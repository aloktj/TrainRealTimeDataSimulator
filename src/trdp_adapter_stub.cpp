#include "data_marshalling.hpp"
#include "diagnostic_manager.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

#include <nlohmann/json.hpp>

namespace trdp_sim::trdp
{

    namespace
    {

        std::string buildPcapEventJson(uint32_t comId, std::size_t len, const std::string& dir)
        {
            return std::string("{\"comId\":") + std::to_string(comId) + ",\"bytes\":" + std::to_string(len) +
                   ",\"direction\":\"" + dir + "\"}";
        }

        void updateMulticastState(trdp_sim::EngineContext& ctx, const std::string& ifaceName, const std::string& group,
                                  const std::optional<std::string>& nic,
                                  const std::optional<std::string>& hostIp, bool joined)
        {
            std::lock_guard<std::mutex> lk(ctx.multicastMtx);
            auto                        it = std::find_if(ctx.multicastGroups.begin(), ctx.multicastGroups.end(),
                                                          [&](const auto& g)
                                                          { return g.ifaceName == ifaceName && g.address == group; });
            if (it == ctx.multicastGroups.end())
            {
                trdp_sim::EngineContext::MulticastGroupState state;
                state.ifaceName = ifaceName;
                state.address   = group;
                state.nic       = nic;
                state.hostIp    = hostIp;
                state.joined    = joined;
                ctx.multicastGroups.push_back(std::move(state));
            }
            else
            {
                it->joined = joined;
                if (nic)
                    it->nic = nic;
                if (hostIp)
                    it->hostIp = hostIp;
            }
        }

    } // namespace

    TrdpAdapter::TrdpAdapter(EngineContext& ctx) : m_ctx(ctx) {}

    bool TrdpAdapter::init()
    {
        m_ctx.trdpSession = reinterpret_cast<TRDP_APP_SESSION_T>(0x1);
        return true;
    }

    void TrdpAdapter::deinit()
    {
        m_ctx.trdpSession = nullptr;
    }

    void TrdpAdapter::applyMulticastConfig(const config::BusInterfaceConfig& iface)
    {
        for (const auto& group : iface.multicastGroups)
            joinMulticast(iface.name, group.address, group.nic, iface.hostIp);
    }

    bool TrdpAdapter::joinMulticast(const std::string& ifaceName, const std::string& group,
                                    const std::optional<std::string>& nic, const std::optional<std::string>& hostIp)
    {
        if (group.empty())
            return false;

        {
            std::lock_guard<std::mutex> lk(m_multicastMtx);
            auto&                       groups = m_multicastMembership[ifaceName];
            if (groups.find(group) != groups.end())
                return true;
            groups.insert(group);
        }

        updateMulticastState(m_ctx, ifaceName, group, nic, hostIp, true);
        if (m_ctx.diagManager)
        {
            nlohmann::json extra{{"iface", ifaceName}, {"group", group}};
            if (nic)
                extra["nic"] = *nic;
            m_ctx.diagManager->log(diag::Severity::INFO, "TRDP", "Joined multicast group", extra.dump());
        }
        return true;
    }

    bool TrdpAdapter::leaveMulticast(const std::string& ifaceName, const std::string& group)
    {
        bool removed{false};
        {
            std::lock_guard<std::mutex> lk(m_multicastMtx);
            auto                        it = m_multicastMembership.find(ifaceName);
            if (it != m_multicastMembership.end())
            {
                removed = it->second.erase(group) > 0;
                if (it->second.empty())
                    m_multicastMembership.erase(it);
            }
        }

        updateMulticastState(m_ctx, ifaceName, group, std::nullopt, std::nullopt, false);
        if (removed && m_ctx.diagManager)
        {
            nlohmann::json extra{{"iface", ifaceName}, {"group", group}};
            m_ctx.diagManager->log(diag::Severity::INFO, "TRDP", "Left multicast group", extra.dump());
        }
        return removed;
    }

    bool TrdpAdapter::recoverInterface(const config::BusInterfaceConfig& iface)
    {
        // Drop any cached membership and reapply desired configuration
        std::vector<std::string> currentGroups;
        {
            std::lock_guard<std::mutex> lk(m_multicastMtx);
            auto                        it = m_multicastMembership.find(iface.name);
            if (it != m_multicastMembership.end())
                currentGroups.assign(it->second.begin(), it->second.end());
        }

        for (const auto& group : currentGroups)
            leaveMulticast(iface.name, group);

        applyMulticastConfig(iface);
        return true;
    }

    int TrdpAdapter::publishPd(engine::pd::PdTelegramRuntime& pd)
    {
        (void) pd;
        return 0;
    }

    int TrdpAdapter::subscribePd(engine::pd::PdTelegramRuntime& pd)
    {
        (void) pd;
        return 0;
    }

    int TrdpAdapter::sendPdData(engine::pd::PdTelegramRuntime& pd, const std::vector<uint8_t>& payload)
    {
        const int rcOverride = m_pdSendResult.value_or(0);
        if (rcOverride != 0)
        {
            recordError(static_cast<uint32_t>(-rcOverride), &TrdpErrorCounters::pdSendErrors);
            return rcOverride;
        }

        trdp_sim::SimulationControls::RedundancySimulation redundancy{};
        {
            std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
            redundancy = m_ctx.simulation.redundancy;
            if (redundancy.forceSwitch && !pd.pubChannels.empty())
            {
                pd.activeChannel = static_cast<uint32_t>((pd.activeChannel + 1) % pd.pubChannels.size());
                pd.stats.redundancySwitches++;
            }
        }

        auto sendOnce = [&](std::size_t channelIdx) -> int {
            if (redundancy.busFailure && redundancy.failedChannel == channelIdx)
            {
                pd.stats.busFailureDrops++;
                recordSendLog(pd.cfg ? pd.cfg->comId : 0, static_cast<uint32_t>(channelIdx), true);
                return kPdSoftDropCode;
            }
            recordSendLog(pd.cfg ? pd.cfg->comId : 0, static_cast<uint32_t>(channelIdx), false);
            return 0;
        };

        bool sentSuccessfully{false};
        int  dropCode{0};
        if (pd.cfg && pd.cfg->pdParam && pd.cfg->pdParam->redundant > 0)
        {
            for (std::size_t i = 0; i < pd.pubChannels.size(); ++i)
            {
                const int rc = sendOnce(i);
                if (rc == 0)
                {
                    sentSuccessfully = true;
                }
                else if (rc == kPdSoftDropCode)
                {
                    dropCode = rc;
                }
                else
                {
                    return rc;
                }
            }
        }
        else
        {
            const std::size_t idx = pd.activeChannel % (pd.pubChannels.empty() ? 1 : pd.pubChannels.size());
            const int         rc  = sendOnce(idx);
            if (rc == 0)
                pd.activeChannel = static_cast<uint32_t>((idx + 1) % (pd.pubChannels.empty() ? 1 : pd.pubChannels.size()));
            else if (rc == kPdSoftDropCode)
                dropCode = rc;
            else
                return rc;
        }

        {
            std::lock_guard<std::mutex> lk(m_errMtx);
            m_lastPdPayload = payload;
        }
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "PD", "PD packet transmitted",
                                   buildPcapEventJson(pd.cfg ? pd.cfg->comId : 0, payload.size(), "tx"));
        }
        if (sentSuccessfully)
            return 0;
        if (dropCode)
            return dropCode;
        return 0;
    }

    void TrdpAdapter::handlePdCallback(uint32_t comId, const uint8_t* data, std::size_t len)
    {
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(data, len, false);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "PD", "PD packet received",
                                   buildPcapEventJson(comId, len, "rx"));
        }
        if (m_ctx.pdEngine)
        {
            m_ctx.pdEngine->onPdReceived(comId, data, len);
        }
    }

    int TrdpAdapter::sendMdRequest(engine::md::MdSessionRuntime& session, const std::vector<uint8_t>& payload)
    {
        const int rc = m_mdRequestResult.value_or(0);
        if (rc != 0)
        {
            recordError(static_cast<uint32_t>(-rc), &TrdpErrorCounters::mdRequestErrors);
            return rc;
        }
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_requestedSessions.push_back(session.sessionId);
        m_lastMdRequestPayload = payload;
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD request sent",
                                   buildPcapEventJson(session.comId, payload.size(), "tx"));
        }
        (void) session;
        return 0;
    }

    int TrdpAdapter::sendMdReply(engine::md::MdSessionRuntime& session, const std::vector<uint8_t>& payload)
    {
        const int rc = m_mdReplyResult.value_or(0);
        if (rc != 0)
        {
            recordError(static_cast<uint32_t>(-rc), &TrdpErrorCounters::mdReplyErrors);
            return rc;
        }
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_repliedSessions.push_back(session.sessionId);
        m_lastMdReplyPayload = payload;
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD reply sent",
                                   buildPcapEventJson(session.comId, payload.size(), "tx"));
        }
        (void) session;
        return 0;
    }

    void TrdpAdapter::handleMdCallback(const TRDP_MD_INFO_T* info, const uint8_t* data, std::size_t len)
    {
        if (m_ctx.diagManager)
        {
            const uint32_t comId = info ? info->comId : 0;
            m_ctx.diagManager->writePacketToPcap(data, len, false);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD packet received",
                                   buildPcapEventJson(comId, len, "rx"));
        }
        if (m_ctx.mdEngine)
        {
            m_ctx.mdEngine->onMdIndication(info, data, len);
        }
    }

    void TrdpAdapter::processOnce() {}

    TrdpErrorCounters TrdpAdapter::getErrorCounters() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_errorCounters;
    }

    std::optional<uint32_t> TrdpAdapter::getLastErrorCode() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_lastErrorCode;
    }

    std::vector<PdSendLogEntry> TrdpAdapter::getPdSendLog() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_pdSendLog;
    }

    void TrdpAdapter::recordError(uint32_t code, uint64_t TrdpErrorCounters::* member)
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_errorCounters.*member += 1;
        m_lastErrorCode = code;
    }

    void TrdpAdapter::recordSendLog(uint32_t comId, uint32_t channel, bool dropped) const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        if (m_pdSendLog.size() > 63)
            m_pdSendLog.erase(m_pdSendLog.begin());
        m_pdSendLog.push_back(PdSendLogEntry{comId, channel, dropped});
    }

    std::vector<trdp_sim::EngineContext::MulticastGroupState> TrdpAdapter::getMulticastState() const
    {
        std::lock_guard<std::mutex> lk(m_ctx.multicastMtx);
        return m_ctx.multicastGroups;
    }

    std::vector<uint8_t> TrdpAdapter::getLastPdPayload() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_lastPdPayload;
    }

    std::vector<uint8_t> TrdpAdapter::getLastMdRequestPayload() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_lastMdRequestPayload;
    }

    std::vector<uint8_t> TrdpAdapter::getLastMdReplyPayload() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_lastMdReplyPayload;
    }

    std::vector<uint32_t> TrdpAdapter::getRequestedSessions() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_requestedSessions;
    }

    std::vector<uint32_t> TrdpAdapter::getRepliedSessions() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_repliedSessions;
    }

    void TrdpAdapter::setPdSendResult(int rc)
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_pdSendResult = rc;
    }

    void TrdpAdapter::setMdRequestResult(int rc)
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_mdRequestResult = rc;
    }

    void TrdpAdapter::setMdReplyResult(int rc)
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_mdReplyResult = rc;
    }

} // namespace trdp_sim::trdp
