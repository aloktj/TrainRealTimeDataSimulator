#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace engine::pd {

namespace {

std::size_t elementTypeSize(data::ElementType type)
{
    switch (type) {
    case data::ElementType::BOOL8:
    case data::ElementType::CHAR8:
    case data::ElementType::INT8:
    case data::ElementType::UINT8:
        return 1;
    case data::ElementType::UTF16:
    case data::ElementType::INT16:
    case data::ElementType::UINT16:
        return 2;
    case data::ElementType::INT32:
    case data::ElementType::UINT32:
    case data::ElementType::REAL32:
    case data::ElementType::TIMEDATE32:
        return 4;
    case data::ElementType::INT64:
    case data::ElementType::UINT64:
    case data::ElementType::REAL64:
    case data::ElementType::TIMEDATE64:
        return 8;
    case data::ElementType::TIMEDATE48:
        return 6;
    case data::ElementType::NESTED_DATASET:
        return 0; // Determined dynamically
    }
    return 0;
}

std::size_t elementSize(const data::ElementDef& def, const trdp_sim::EngineContext& ctx)
{
    if (def.type == data::ElementType::NESTED_DATASET) {
        if (!def.nestedDataSetId)
            return 0;
        auto it = ctx.dataSetDefs.find(*def.nestedDataSetId);
        if (it == ctx.dataSetDefs.end())
            return 0;
        std::size_t nestedSize = 0;
        for (const auto& nestedEl : it->second.elements)
            nestedSize += elementSize(nestedEl, ctx);
        return nestedSize * def.arraySize;
    }
    return elementTypeSize(def.type) * def.arraySize;
}

std::vector<uint8_t> marshalDataSet(const data::DataSetInstance& inst, const trdp_sim::EngineContext& ctx)
{
    std::vector<uint8_t> out;
    if (!inst.def)
        return out;

    for (std::size_t idx = 0; idx < inst.def->elements.size() && idx < inst.values.size(); ++idx) {
        const auto& el = inst.def->elements[idx];
        const auto& cell = inst.values[idx];
        const auto expectedSize = elementSize(el, ctx);
        if (!cell.defined) {
            out.insert(out.end(), expectedSize, 0);
            continue;
        }

        if (cell.raw.size() >= expectedSize) {
            out.insert(out.end(), cell.raw.begin(), cell.raw.begin() + expectedSize);
        } else {
            out.insert(out.end(), cell.raw.begin(), cell.raw.end());
            out.insert(out.end(), expectedSize - cell.raw.size(), 0);
        }
    }

    return out;
}

void unmarshalDataToDataSet(data::DataSetInstance& inst, const trdp_sim::EngineContext& ctx, const uint8_t* data, std::size_t len)
{
    if (!inst.def)
        return;

    std::size_t offset = 0;
    for (std::size_t idx = 0; idx < inst.def->elements.size() && idx < inst.values.size(); ++idx) {
        const auto& el = inst.def->elements[idx];
        auto& cell = inst.values[idx];
        const auto expectedSize = elementSize(el, ctx);
        if (expectedSize == 0)
            continue;

        if (offset >= len) {
            cell.raw.assign(expectedSize, 0);
            cell.defined = false;
            continue;
        }

        auto remaining = len - offset;
        auto toCopy = std::min<std::size_t>(expectedSize, remaining);
        cell.raw.assign(data + offset, data + offset + toCopy);
        if (toCopy < expectedSize)
            cell.raw.resize(expectedSize, 0);
        cell.defined = true;
        offset += expectedSize;
    }
}

} // namespace

PdEngine::PdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter)
    : m_ctx(ctx)
    , m_adapter(adapter)
{
}

PdEngine::~PdEngine()
{
    stop();
}

void PdEngine::initializeFromConfig()
{
    m_ctx.pdTelegrams.clear();

    for (const auto& iface : m_ctx.deviceConfig.interfaces) {
        for (const auto& tel : iface.telegrams) {
            if (!tel.pdParam)
                continue; // Skip MD telegrams

            auto dsIt = m_ctx.dataSetInstances.find(tel.dataSetId);
            if (dsIt == m_ctx.dataSetInstances.end()) {
                std::cerr << "Dataset instance missing for PD COM ID " << tel.comId << std::endl;
                continue;
            }

            auto rt = std::make_unique<PdTelegramRuntime>();
            rt->cfg = &tel;
            rt->ifaceCfg = &iface;
            rt->pdComCfg = &iface.pdCom;
            rt->dataset = dsIt->second.get();
            rt->dataset->isOutgoing = !tel.destinations.empty();
            rt->direction = tel.destinations.empty() ? Direction::SUBSCRIBE : Direction::PUBLISH;
            rt->enabled = true;

            if (rt->direction == Direction::SUBSCRIBE) {
                int rc = m_adapter.subscribePd(*rt);
                if (rc != 0)
                    std::cerr << "Failed to subscribe PD COM ID " << tel.comId << " (rc=" << rc << ")" << std::endl;
            } else {
                int rc = m_adapter.publishPd(*rt);
                if (rc != 0)
                    std::cerr << "Failed to publish PD COM ID " << tel.comId << " (rc=" << rc << ")" << std::endl;
            }

            m_ctx.pdTelegrams.push_back(std::move(rt));
        }
    }
}

void PdEngine::start()
{
    if (m_running.exchange(true))
        return;

    m_thread = std::thread(&PdEngine::runPublisherLoop, this);
}

void PdEngine::stop()
{
    if (!m_running.exchange(false))
        return;

    if (m_thread.joinable())
        m_thread.join();
}

void PdEngine::enableTelegram(uint32_t comId, bool enable)
{
    for (auto& pdPtr : m_ctx.pdTelegrams) {
        auto& pd = *pdPtr;
        if (pd.cfg && pd.cfg->comId == comId) {
            std::lock_guard<std::mutex> lk(pd.mtx);
            pd.enabled = enable;
        }
    }
}

data::DataSetInstance* PdEngine::getDataSetInstance(uint32_t dataSetId)
{
    auto it = m_ctx.dataSetInstances.find(dataSetId);
    if (it == m_ctx.dataSetInstances.end())
        return nullptr;
    return it->second.get();
}

void PdEngine::onPdReceived(uint32_t comId, const uint8_t* data, std::size_t len)
{
    for (auto& pdPtr : m_ctx.pdTelegrams) {
        auto& pd = *pdPtr;
        if (!pd.cfg || pd.cfg->comId != comId || pd.direction != Direction::SUBSCRIBE)
            continue;

        std::lock_guard<std::mutex> lk(pd.mtx);
        auto* ds = pd.dataset;
        if (!ds)
            continue;

        auto now = std::chrono::steady_clock::now();
        auto timeoutUs = pd.cfg->pdParam ? pd.cfg->pdParam->timeoutUs : pd.pdComCfg->timeoutUs;
        auto cycleUs = pd.cfg->pdParam ? pd.cfg->pdParam->cycleUs : 0u;
        if (pd.stats.lastRxTime.time_since_epoch().count() != 0) {
            auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - pd.stats.lastRxTime);
            if (cycleUs > 0) {
                auto jitter = static_cast<double>(delta.count()) - static_cast<double>(cycleUs);
                pd.stats.lastCycleJitterUs = std::abs(jitter);
            }
            if (timeoutUs > 0 && static_cast<uint64_t>(delta.count()) > timeoutUs)
                pd.stats.timeoutCount++;
        }

        pd.stats.rxCount++;
        pd.stats.lastSeqNumber++;
        pd.stats.lastRxTime = now;

        std::lock_guard<std::mutex> dsLock(ds->mtx);
        if (!ds->locked)
            unmarshalDataToDataSet(*ds, m_ctx, data, len);
    }
}

void PdEngine::runPublisherLoop()
{
    using namespace std::chrono;
    while (m_running.load()) {
        auto now = steady_clock::now();
        for (auto& pdPtr : m_ctx.pdTelegrams) {
            auto& pd = *pdPtr;
            std::lock_guard<std::mutex> lk(pd.mtx);
            if (!pd.enabled || !pd.cfg || !pd.cfg->pdParam || pd.direction != Direction::PUBLISH)
                continue;

            auto cycle = microseconds(pd.cfg->pdParam->cycleUs);
            if (pd.stats.lastTxTime.time_since_epoch().count() == 0 ||
                now - pd.stats.lastTxTime >= cycle) {

                auto* ds = pd.dataset;
                if (!ds)
                    continue;

                std::lock_guard<std::mutex> dsLock(ds->mtx);
                auto payload = marshalDataSet(*ds, m_ctx);
                int rc = m_adapter.sendPdData(pd, payload);
                if (rc == 0) {
                    pd.stats.txCount++;
                    pd.stats.lastSeqNumber++;
                    pd.stats.lastTxTime = now;
                } else {
                    std::cerr << "Failed to send PD COM ID " << pd.cfg->comId << " (rc=" << rc << ")" << std::endl;
                }
            }
        }
        std::this_thread::sleep_for(1ms);
    }
}

} // namespace engine::pd
