#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace engine::pd {

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
    (void)len;

    for (auto& pdPtr : m_ctx.pdTelegrams) {
        auto& pd = *pdPtr;
        if (!pd.cfg || pd.cfg->comId != comId)
            continue;

        std::lock_guard<std::mutex> lk(pd.mtx);
        // TODO: unmarshal into pd.dataset
        // update stats
        pd.stats.rxCount++;
        pd.stats.lastRxTime = std::chrono::steady_clock::now();
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
            if (!pd.enabled || !pd.cfg || !pd.cfg->pdParam)
                continue;

            auto cycle = microseconds(pd.cfg->pdParam->cycleUs);
            if (pd.stats.lastTxTime.time_since_epoch().count() == 0 ||
                now - pd.stats.lastTxTime >= cycle) {

                // TODO: marshal dataset and send via m_adapter.publishPd(pd)
                m_adapter.publishPd(pd);
                pd.stats.txCount++;
                pd.stats.lastTxTime = now;
            }
        }
        std::this_thread::sleep_for(1ms);
    }
}

} // namespace engine::pd
