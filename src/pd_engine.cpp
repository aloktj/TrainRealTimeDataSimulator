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
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace engine::pd
{

    namespace
    {
        using Rule = trdp_sim::SimulationControls::InjectionRule;

        TRDP_IP_ADDR_T parseIp(const std::string& ip)
        {
            if (ip.empty())
                return 0;

            struct in_addr addr
            {
            };

            if (inet_aton(ip.c_str(), &addr) == 0)
                return 0;

            /* TRDP expects host-order IP integers. */
            return static_cast<TRDP_IP_ADDR_T>(ntohl(addr.s_addr));
        }

        std::optional<Rule> findRule(const trdp_sim::EngineContext& ctx, uint32_t comId, uint32_t dataSetId)
        {
            std::lock_guard<std::mutex> lk(ctx.simulation.mtx);
            auto                        it = ctx.simulation.pdRules.find(comId);
            if (it != ctx.simulation.pdRules.end())
                return it->second;
            auto dsIt = ctx.simulation.dataSetRules.find(dataSetId);
            if (dsIt != ctx.simulation.dataSetRules.end())
                return dsIt->second;
            return std::nullopt;
        }

        bool shouldDrop(const Rule& rule)
        {
            if (rule.lossRate <= 0.0)
                return false;
            thread_local std::mt19937_64 rng{std::random_device{}()};
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return dist(rng) < rule.lossRate;
        }

        void applyDelay(const Rule& rule)
        {
            if (rule.delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(rule.delayMs));
        }
    } // namespace

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
                rt->redundantActive     = tel.pdParam && tel.pdParam->redundant > 0;
                if (!tel.destinations.empty())
                {
                    for (const auto& dest : tel.destinations)
                    {
                        PdTelegramRuntime::PublicationChannel ch{};
                        ch.destIp = parseIp(dest.uri);
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
        const auto baseRule = findRule(m_ctx, comId, 0);
        if (baseRule)
        {
            if (shouldDrop(*baseRule))
                return;
            applyDelay(*baseRule);
        }
        const uint32_t targetComId = (baseRule && baseRule->corruptComId) ? (comId ^ 0x1u) : comId;

        for (auto& pdPtr : m_ctx.pdTelegrams)
        {
            auto& pd = *pdPtr;
            if (!pd.cfg || pd.cfg->comId != targetComId || pd.direction != Direction::SUBSCRIBE)
                continue;

            std::lock_guard<std::mutex> lk(pd.mtx);
            auto*                       ds = pd.dataset;
            if (!ds)
                continue;

            auto rule = findRule(m_ctx, pd.cfg->comId, pd.cfg->dataSetId);
            if (!rule)
                rule = baseRule;
            if (rule && rule->seqDelta != 0)
            {
                auto next = static_cast<int64_t>(pd.stats.lastSeqNumber) + rule->seqDelta;
                pd.stats.lastSeqNumber = next < 0 ? 0 : static_cast<uint64_t>(next);
            }

            std::vector<uint8_t> mutatedPayload;
            const uint8_t*       payloadPtr = data;
            if (rule && rule->corruptDataSetId && data && len > 0)
            {
                mutatedPayload.assign(data, data + len);
                mutatedPayload[0] = static_cast<uint8_t>(mutatedPayload[0] ^ 0xFF);
                payloadPtr        = mutatedPayload.data();
            }

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
                    unmarshalDataToDataSet(*ds, m_ctx, payloadPtr, len);
                }
                else if (!ds->values.empty())
                {
                    auto& cell = ds->values.front();
                    cell.raw.assign(payloadPtr, payloadPtr + len);
                    cell.defined = true;
                }
            }
        }
    }

    void PdEngine::processPublishersOnce(std::chrono::steady_clock::time_point now)
{
    using namespace std::chrono;
    using trdp_sim::util::marshalDataSet;

    trdp_sim::SimulationControls::StressMode stressSnapshot{};
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        stressSnapshot = m_ctx.simulation.stress;
    }
    const bool  stressActive = stressSnapshot.enabled;
    std::size_t pdBudget     = stressActive
                                  ? std::min<std::size_t>(stressSnapshot.pdBurstTelegrams,
                                                          trdp_sim::SimulationControls::StressMode::kMaxBurstTelegrams)
                                  : 0;
    const auto minCycle = microseconds(trdp_sim::SimulationControls::StressMode::kMinCycleUs);

    struct Candidate
    {
        PdTelegramRuntime*                       pd{nullptr};
        microseconds                             cycle{};
        std::chrono::steady_clock::time_point    nextDue{};
    };

    std::vector<Candidate> due;
    for (auto& pdPtr : m_ctx.pdTelegrams)
    {
        auto&                       pd = *pdPtr;
        std::lock_guard<std::mutex> lk(pd.mtx);
        if (!pd.enabled || !pd.cfg || !pd.cfg->pdParam || pd.direction != Direction::PUBLISH)
            continue;

        auto cycle = microseconds(pd.cfg->pdParam->cycleUs);
        if (stressActive && stressSnapshot.pdCycleOverrideUs > 0)
        {
            auto overrideCycle = microseconds(
                std::max(stressSnapshot.pdCycleOverrideUs,
                         static_cast<uint32_t>(trdp_sim::SimulationControls::StressMode::kMinCycleUs)));
            if (overrideCycle < cycle || pd.cfg->pdParam->cycleUs == 0)
                cycle = overrideCycle;
        }
        if (cycle < minCycle)
            cycle = minCycle;
        if (pd.stats.lastTxTime.time_since_epoch().count() == 0)
            pd.stats.lastTxTime = now - cycle;

        const auto nextDue = pd.stats.lastTxTime + cycle;
        bool       isDue   = pd.sendNow || now >= nextDue;
        if (!isDue && stressActive && pdBudget > 0)
        {
            isDue = true;
            --pdBudget;
            pd.stats.stressBursts++;
        }

        if (isDue)
            due.push_back(Candidate{&pd, cycle, nextDue});
    }

    std::sort(due.begin(), due.end(), [](const Candidate& a, const Candidate& b) {
        if (a.nextDue == b.nextDue)
            return (a.pd && b.pd && a.pd->cfg && b.pd->cfg) ? a.pd->cfg->comId < b.pd->cfg->comId : a.nextDue < b.nextDue;
        return a.nextDue < b.nextDue;
    });

    for (auto& item : due)
    {
        auto&                       pd = *item.pd;
        std::lock_guard<std::mutex> lk(pd.mtx);
        auto*                       ds = pd.dataset;
        if (!ds)
            continue;

        const auto rule = pd.cfg ? findRule(m_ctx, pd.cfg->comId, pd.cfg->dataSetId) : std::nullopt;
        if (rule)
        {
            if (shouldDrop(*rule))
                continue;
            applyDelay(*rule);
            if (rule->corruptComId && m_ctx.diagManager)
            {
                m_ctx.diagManager->log(diag::Severity::WARN, "PD", "Injecting COM ID corruption for PD telegram");
            }
        }

        std::lock_guard<std::mutex> dsLock(ds->mtx);
        const bool                  shouldMarshall = pd.cfg->pdParam ? pd.cfg->pdParam->marshall : pd.pdComCfg->marshall;
        auto payload = shouldMarshall ? marshalDataSet(*ds, m_ctx)
                                      : std::vector<uint8_t>(ds->values.empty() ? 0 : ds->values.front().raw.size());
        if (!shouldMarshall && !ds->values.empty())
        {
            payload = ds->values.front().raw;
        }

        if (rule && rule->corruptDataSetId && !payload.empty())
            payload[0] = static_cast<uint8_t>(payload[0] ^ 0xFF);
        if (rule && rule->corruptComId)
            payload.insert(payload.begin(), 0xCD);

        if (rule && rule->seqDelta != 0)
        {
            auto next = static_cast<int64_t>(pd.stats.lastSeqNumber) + rule->seqDelta;
            pd.stats.lastSeqNumber = next < 0 ? 0 : static_cast<uint64_t>(next);
        }

        int rc = m_adapter.sendPdData(pd, payload);
        if (rc == 0 || rc == trdp_sim::trdp::kPdSoftDropCode)
        {
            if (rc == 0)
                pd.stats.txCount++;
            pd.stats.lastSeqNumber++;
            pd.stats.lastTxTime = now;
            pd.stats.lastTxWall = std::chrono::system_clock::now();
            pd.sendNow          = false;
        }
        else
        {
            std::cerr << "Failed to send PD COM ID " << (pd.cfg ? pd.cfg->comId : 0) << " (rc=" << rc << ")" << std::endl;
        }
    }
}

void PdEngine::runPublisherLoop()
{
    using namespace std::chrono_literals;

    while (m_running.load())
    {
        const auto now = std::chrono::steady_clock::now();
        processPublishersOnce(now);

        for (auto& pdPtr : m_ctx.pdTelegrams)
        {
            auto&                       pd = *pdPtr;
            std::lock_guard<std::mutex> lk(pd.mtx);
            if (pd.direction == Direction::SUBSCRIBE && pd.cfg && pd.cfg->pdParam)
            {
                auto timeoutUs = pd.cfg->pdParam->timeoutUs;
                if (timeoutUs > 0 && pd.stats.lastRxTime.time_since_epoch().count() != 0)
                {
                    auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - pd.stats.lastRxTime);
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
