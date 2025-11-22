#include "pd_engine.hpp"
#include "data_marshalling.hpp"
#include "diagnostic_manager.hpp"
#include "trdp_adapter.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace engine::pd
{

    using trdp_sim::util::marshalDataSet;
    using trdp_sim::util::unmarshalDataToDataSet;
    using PdRuntimePtr = std::unique_ptr<PdTelegramRuntime, trdp_sim::PdRuntimeDeleter>;

    PdEngine::PdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter)
        : m_ctx(ctx), m_adapter(adapter)
    {
    }

    PdEngine::~PdEngine()
    {
        stop();
    }

    void PdEngine::initializeFromConfig()
    {
        m_ctx.pdTelegrams.clear();

        for (const auto& iface : m_ctx.deviceConfig.interfaces)
        {
            m_adapter.applyMulticastConfig(iface);
            for (const auto& tel : iface.telegrams)
            {
                if (!tel.pdParam)
                    continue; // Skip MD telegrams

                auto dsIt = m_ctx.dataSetInstances.find(tel.dataSetId);
                if (dsIt == m_ctx.dataSetInstances.end())
                {
                    std::cerr << "Dataset instance missing for PD COM ID " << tel.comId << std::endl;
                    continue;
                }

                PdRuntimePtr rt(new PdTelegramRuntime());
                rt->cfg                 = &tel;
                rt->ifaceCfg            = &iface;
                rt->pdComCfg            = &iface.pdCom;
                rt->dataset             = dsIt->second.get();
                rt->dataset->isOutgoing = !tel.destinations.empty();
                rt->direction           = tel.destinations.empty() ? Direction::SUBSCRIBE : Direction::PUBLISH;
                rt->enabled             = true;
                rt->activeChannel       = 0;
                if (!tel.destinations.empty())
                {
                    for (const auto& dest : tel.destinations)
                    {
                        PdTelegramRuntime::PublicationChannel ch{};
                        ch.destIp = dest.uri.empty() ? 0 : inet_addr(dest.uri.c_str());
                        rt->pubChannels.push_back(ch);
                    }

                    if (rt->pubChannels.empty())
                    {
                        PdTelegramRuntime::PublicationChannel ch{};
                        ch.destIp = 0;
                        rt->pubChannels.push_back(ch);
                    }
                }

                if (rt->direction == Direction::SUBSCRIBE)
                {
                    int rc = m_adapter.subscribePd(*rt);
                    if (rc != 0)
                        std::cerr << "Failed to subscribe PD COM ID " << tel.comId << " (rc=" << rc << ")" << std::endl;
                }
                else
                {
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
        for (auto& pdPtr : m_ctx.pdTelegrams)
        {
            auto& pd = *pdPtr;
            if (pd.cfg && pd.cfg->comId == comId)
            {
                std::lock_guard<std::mutex> lk(pd.mtx);
                pd.enabled = enable;
                if (m_ctx.diagManager)
                {
                    m_ctx.diagManager->log(diag::Severity::INFO, "PD",
                                           std::string("PD COM ID ") + std::to_string(comId) +
                                               (enable ? " enabled" : " disabled"));
                }
            }
        }
    }

    void PdEngine::triggerSendNow(uint32_t comId)
    {
        for (auto& pdPtr : m_ctx.pdTelegrams)
        {
            auto& pd = *pdPtr;
            if (pd.cfg && pd.cfg->comId == comId && pd.direction == Direction::PUBLISH)
            {
                std::lock_guard<std::mutex> lk(pd.mtx);
                pd.sendNow = true;
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
        for (auto& pdPtr : m_ctx.pdTelegrams)
        {
            auto& pd = *pdPtr;
            if (!pd.cfg || pd.cfg->comId != comId || pd.direction != Direction::SUBSCRIBE)
                continue;

            std::lock_guard<std::mutex> lk(pd.mtx);
            auto*                       ds = pd.dataset;
            if (!ds)
                continue;

            auto now       = std::chrono::steady_clock::now();
            auto timeoutUs = pd.cfg->pdParam ? pd.cfg->pdParam->timeoutUs : pd.pdComCfg->timeoutUs;
            auto cycleUs   = pd.cfg->pdParam ? pd.cfg->pdParam->cycleUs : 0u;
            if (pd.stats.lastRxTime.time_since_epoch().count() != 0)
            {
                auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - pd.stats.lastRxTime);
                pd.stats.lastInterarrivalUs = static_cast<double>(delta.count());
                if (cycleUs > 0)
                {
                    auto jitter                = static_cast<double>(delta.count()) - static_cast<double>(cycleUs);
                    pd.stats.lastCycleJitterUs = std::abs(jitter);
                }
                if (timeoutUs > 0 && static_cast<uint64_t>(delta.count()) > timeoutUs)
                {
                    pd.stats.timeoutCount++;
                    pd.stats.timedOut = true;
                    if (pd.cfg->pdParam &&
                        pd.cfg->pdParam->validityBehavior == config::PdComParameter::ValidityBehavior::ZERO)
                    {
                        std::lock_guard<std::mutex> dsLock(ds->mtx);
                        for (auto& cell : ds->values)
                        {
                            cell.defined = false;
                            std::fill(cell.raw.begin(), cell.raw.end(), 0);
                        }
                    }
                }
            }

            pd.stats.rxCount++;
            pd.stats.lastSeqNumber++;
            pd.stats.lastRxTime = now;
            pd.stats.lastRxWall = std::chrono::system_clock::now();
            pd.stats.timedOut   = false;

            std::lock_guard<std::mutex> dsLock(ds->mtx);
            if (!ds->locked)
            {
                const bool shouldMarshall = pd.cfg->pdParam ? pd.cfg->pdParam->marshall : pd.pdComCfg->marshall;
                if (shouldMarshall)
                {
                    unmarshalDataToDataSet(*ds, m_ctx, data, len);
                }
                else if (!ds->values.empty())
                {
                    auto& cell = ds->values.front();
                    cell.raw.assign(data, data + len);
                    cell.defined = true;
                }
            }
        }
    }

    void PdEngine::runPublisherLoop()
    {
        using namespace std::chrono;
        using trdp_sim::util::marshalDataSet;
        while (m_running.load())
        {
            auto now = steady_clock::now();
            for (auto& pdPtr : m_ctx.pdTelegrams)
            {
                auto&                       pd = *pdPtr;
                std::lock_guard<std::mutex> lk(pd.mtx);
                if (!pd.enabled || !pd.cfg || !pd.cfg->pdParam || pd.direction != Direction::PUBLISH)
                    continue;

                auto cycle = microseconds(pd.cfg->pdParam->cycleUs);
                if (pd.sendNow || pd.stats.lastTxTime.time_since_epoch().count() == 0 || now - pd.stats.lastTxTime >= cycle)
                {
                    auto* ds = pd.dataset;
                    if (!ds)
                        continue;

                    std::lock_guard<std::mutex> dsLock(ds->mtx);
                    const bool                  shouldMarshall = pd.cfg->pdParam ? pd.cfg->pdParam->marshall :
                                                                   pd.pdComCfg->marshall;
                    auto payload = shouldMarshall ? marshalDataSet(*ds, m_ctx)
                                                  : std::vector<uint8_t>(ds->values.empty() ? 0 : ds->values.front().raw.size());
                    if (!shouldMarshall && !ds->values.empty())
                    {
                        payload = ds->values.front().raw;
                    }

                    int rc = m_adapter.sendPdData(pd, payload);
                    if (rc == 0)
                    {
                        pd.stats.txCount++;
                        pd.stats.lastSeqNumber++;
                        pd.stats.lastTxTime = now;
                        pd.stats.lastTxWall = std::chrono::system_clock::now();
                        pd.sendNow          = false;
                    }
                    else
                    {
                        std::cerr << "Failed to send PD COM ID " << pd.cfg->comId << " (rc=" << rc << ")" << std::endl;
                    }
                }

                if (pd.direction == Direction::SUBSCRIBE && pd.cfg && pd.cfg->pdParam)
                {
                    auto timeoutUs = pd.cfg->pdParam->timeoutUs;
                    if (timeoutUs > 0 && pd.stats.lastRxTime.time_since_epoch().count() != 0)
                    {
                        auto delta = duration_cast<microseconds>(now - pd.stats.lastRxTime);
                        if (!pd.stats.timedOut && static_cast<uint64_t>(delta.count()) > timeoutUs)
                        {
                            pd.stats.timeoutCount++;
                            pd.stats.timedOut = true;
                        }
                    }
                }
            }
            std::this_thread::sleep_for(1ms);
        }
    }

} // namespace engine::pd
