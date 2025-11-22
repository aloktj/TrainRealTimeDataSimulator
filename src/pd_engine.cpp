#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <chrono>
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
    // TODO: create PdTelegramRuntime instances from m_ctx.deviceConfig
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
