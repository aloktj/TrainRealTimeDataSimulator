#include "trdp_adapter.hpp"

#include "diagnostic_manager.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <sys/select.h>
#include <type_traits>
#include <vector>

#ifndef TRDP_TO_ZERO
#define TRDP_TO_ZERO static_cast<TRDP_TO_BEHAVIOR_T>(0)
#endif

#ifndef TRDP_TO_KEEP_LAST_VALUE
#define TRDP_TO_KEEP_LAST_VALUE static_cast<TRDP_TO_BEHAVIOR_T>(1)
#endif

#if __has_include(<trdp_if_light.h>)
#include <trdp_if_light.h>
#elif __has_include(<trdp_if.h>)
#include <trdp_if.h>
#elif __has_include(<tau_api.h>)
#include <tau_api.h>
#endif

#include "md_engine.hpp"
#include "pd_engine.hpp"

namespace trdp_sim::trdp
{

    namespace
    {

        std::string buildPcapEventJson(uint32_t comId, std::size_t len, const std::string& dir)
        {
            return std::string("{\"comId\":") + std::to_string(comId) + ",\"bytes\":" + std::to_string(len) +
                   ",\"direction\":\"" + dir + "\"}";
        }

        TRDP_IP_ADDR_T toIp(const std::optional<std::string>& maybeIp)
        {
            if (!maybeIp || maybeIp->empty())
                return 0;
            return inet_addr(maybeIp->c_str());
        }

        TRDP_IP_ADDR_T toIp(const std::string& ip)
        {
            if (ip.empty())
                return 0;
            return inet_addr(ip.c_str());
        }

        void pdCallback(void* refCon, TRDP_APP_SESSION_T /*session*/, const TRDP_PD_INFO_T* info, UINT8* data,
                        UINT32 dataSize)
        {
            auto* adapter = static_cast<TrdpAdapter*>(refCon);
            if (adapter && info)
                adapter->handlePdCallback(info->comId, data, dataSize);
        }

        void mdCallback(void* refCon, TRDP_APP_SESSION_T /*session*/, const TRDP_MD_INFO_T* info, UINT8* data,
                        UINT32 dataSize)
        {
            auto* adapter = static_cast<TrdpAdapter*>(refCon);
            if (adapter)
                adapter->handleMdCallback(info, data, dataSize);
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

    namespace
    {
        template <typename T, typename = void>
        struct HasHostNameField : std::false_type
        {
        };

        template <typename T>
        struct HasHostNameField<T, std::void_t<decltype(T::hostName)>> : std::true_type
        {
        };
    } // namespace

    TrdpAdapter::TrdpAdapter(EngineContext& ctx) : m_ctx(ctx) {}

    bool TrdpAdapter::init()
    {
        TRDP_MEM_CONFIG_T memCfg{};

        TRDP_PD_CONFIG_T      pdCfg{};
        TRDP_MD_CONFIG_T      mdCfg{};
        TRDP_PROCESS_CONFIG_T processCfg{};
        if constexpr (HasHostNameField<TRDP_PROCESS_CONFIG_T>::value)
        {
            std::strncpy(processCfg.hostName, m_ctx.deviceConfig.hostName.c_str(), sizeof(processCfg.hostName) - 1U);
            processCfg.hostName[sizeof(processCfg.hostName) - 1U] = '\0';
        }

        TRDP_ERR_T err{TRDP_NO_ERR};
        if constexpr (std::is_invocable_r_v<TRDP_ERR_T, decltype(&tlc_init), TRDP_PRINT_DBG_T, void*,
                                                 const TRDP_MEM_CONFIG_T*>)
        {
            err = tlc_init(nullptr, nullptr, &memCfg);
        }
        else
        {
            err =
#ifdef TRDP_ERR_GENERIC
                TRDP_ERR_GENERIC;
#else
                static_cast<TRDP_ERR_T>(-1);
#endif
        }
        if (err != TRDP_NO_ERR)
        {
            recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::initErrors);
            std::cerr << "TRDP tlc_init failed: " << err << std::endl;
            return false;
        }

        err = tlc_openSession(&m_ctx.trdpSession, 0, 0, nullptr, &pdCfg, &mdCfg, &processCfg);
        if (err != TRDP_NO_ERR)
        {
            recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::initErrors);
            std::cerr << "TRDP tlc_openSession failed: " << err << std::endl;
            return false;
        }

        return m_ctx.trdpSession != nullptr;
    }

    void TrdpAdapter::deinit()
    {
        if (m_ctx.trdpSession)
        {
            tlc_closeSession(m_ctx.trdpSession);
            tlc_terminate();
            m_ctx.trdpSession = nullptr;
        }
    }

    void TrdpAdapter::applyMulticastConfig(const config::BusInterfaceConfig& iface)
    {
        for (const auto& group : iface.multicastGroups)
        {
            joinMulticast(iface.name, group.address, group.nic, iface.hostIp);
        }
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
        std::vector<std::string> currentGroups;
        {
            std::lock_guard<std::mutex> lk(m_multicastMtx);
            auto                        it = m_multicastMembership.find(iface.name);
            if (it != m_multicastMembership.end())
                currentGroups.assign(it->second.begin(), it->second.end());
        }

        for (const auto& group : currentGroups)
        {
            leaveMulticast(iface.name, group);
        }

        applyMulticastConfig(iface);
        return true;
    }

    std::vector<trdp_sim::EngineContext::MulticastGroupState> TrdpAdapter::getMulticastState() const
    {
        std::lock_guard<std::mutex> lk(m_ctx.multicastMtx);
        return m_ctx.multicastGroups;
    }

    int TrdpAdapter::publishPd(engine::pd::PdTelegramRuntime& pd)
    {
        if (!m_ctx.trdpSession || !pd.cfg || !pd.pdComCfg)
            return -1;

        TRDP_IP_ADDR_T srcIp = toIp(pd.ifaceCfg->hostIp);

        if (pd.pubChannels.empty())
        {
            engine::pd::PdTelegramRuntime::PublicationChannel ch{};
            ch.destIp = (!pd.cfg->destinations.empty()) ? toIp(pd.cfg->destinations.front().uri) : 0;
            pd.pubChannels.push_back(ch);
        }

        for (auto& ch : pd.pubChannels)
        {
            if (ch.handle)
                continue;

            TRDP_ERR_T err = tlp_publish(m_ctx.trdpSession, &ch.handle, this, &pdCallback, 0, pd.cfg->comId, 0, 0,
                                         srcIp, ch.destIp, pd.pdComCfg ? pd.pdComCfg->port : 0, 0, 0, nullptr, nullptr,
                                         0);

            if (err != TRDP_NO_ERR)
            {
                recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::publishErrors);
                std::cerr << "tlp_publish failed for COM ID " << pd.cfg->comId << " error=" << err << std::endl;
                if (m_ctx.diagManager)
                    m_ctx.diagManager->log(diag::Severity::ERROR, "PD", "Failed to publish",
                                           buildPcapEventJson(pd.cfg->comId, 0, "tx"));
                return -static_cast<int>(err);
            }
        }
        return 0;
    }

    int TrdpAdapter::subscribePd(engine::pd::PdTelegramRuntime& pd)
    {
        if (!m_ctx.trdpSession || !pd.cfg || !pd.pdComCfg)
            return -1;

        TRDP_IP_ADDR_T srcIp  = 0; // wildcard
        TRDP_IP_ADDR_T destIp = toIp(pd.ifaceCfg->hostIp);

        TRDP_PD_CONFIG_T pdCfg{};
        pdCfg.toBehavior =
            (pd.cfg->pdParam && pd.cfg->pdParam->validityBehavior == config::PdComParameter::ValidityBehavior::ZERO)
                ? TRDP_TO_ZERO
                : TRDP_TO_KEEP_LAST_VALUE;
        pdCfg.timeout = pd.cfg->pdParam ? pd.cfg->pdParam->timeoutUs : pd.pdComCfg->timeoutUs;

        TRDP_ERR_T err = tlp_subscribe(m_ctx.trdpSession, &pd.subHandle, this, &pdCallback, 0, pd.cfg->comId, 0, 0,
                                       srcIp, 0, destIp, pd.pdComCfg ? pd.pdComCfg->port : 0, nullptr, pdCfg.timeout,
                                       pdCfg.toBehavior);

        if (err != TRDP_NO_ERR)
        {
            recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::subscribeErrors);
            std::cerr << "tlp_subscribe failed for COM ID " << pd.cfg->comId << " error=" << err << std::endl;
            if (m_ctx.diagManager)
                m_ctx.diagManager->log(diag::Severity::ERROR, "PD", "Failed to subscribe",
                                       buildPcapEventJson(pd.cfg->comId, 0, "rx"));
            return -static_cast<int>(err);
        }
        return 0;
    }

    int TrdpAdapter::sendPdData(engine::pd::PdTelegramRuntime& pd, const std::vector<uint8_t>& payload)
    {
        if (!m_ctx.trdpSession || !pd.cfg || !pd.pdComCfg)
            return -1;

        {
            std::lock_guard<std::mutex> lk(m_errMtx);
            m_lastPdPayload = payload;
        }

        if (pd.pubChannels.empty())
        {
            int rc = publishPd(pd);
            if (rc != 0)
                return rc;
        }

        const bool sendRedundant = pd.cfg->pdParam && pd.cfg->pdParam->redundant > 0;
        size_t     idx           = pd.activeChannel % (pd.pubChannels.empty() ? 1 : pd.pubChannels.size());

        trdp_sim::SimulationControls::RedundancySimulation redundancy{};
        {
            std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
            redundancy = m_ctx.simulation.redundancy;
            if (redundancy.forceSwitch && !pd.pubChannels.empty())
            {
                idx = (idx + 1) % pd.pubChannels.size();
                pd.stats.redundancySwitches++;
            }
        }

        auto sendOnce = [&](engine::pd::PdTelegramRuntime::PublicationChannel& ch, size_t channelIdx) -> int {
            if (redundancy.busFailure && redundancy.failedChannel == channelIdx)
            {
                if (m_ctx.diagManager)
                {
                    m_ctx.diagManager->log(diag::Severity::WARN, "PD", "Dropping PD due to simulated bus failure");
                }
                pd.stats.busFailureDrops++;
                recordSendLog(pd.cfg ? pd.cfg->comId : 0, static_cast<uint32_t>(channelIdx), true);
                return kPdSoftDropCode;
            }
            if (!ch.handle)
            {
                int rc = publishPd(pd);
                if (rc != 0)
                    return rc;
            }

            TRDP_ERR_T err = tlp_put(m_ctx.trdpSession, ch.handle,
                                     const_cast<UINT8*>(payload.empty() ? nullptr : payload.data()),
                                     static_cast<UINT32>(payload.size()));

            if (err != TRDP_NO_ERR)
            {
                recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::pdSendErrors);
                std::cerr << "tlp_put failed for COM ID " << pd.cfg->comId << " error=" << err << std::endl;
                if (m_ctx.diagManager)
                    m_ctx.diagManager->log(diag::Severity::ERROR, "PD", "PD send failed",
                                           buildPcapEventJson(pd.cfg->comId, payload.size(), "tx"));
                return -static_cast<int>(err);
            }
            recordSendLog(pd.cfg ? pd.cfg->comId : 0, static_cast<uint32_t>(channelIdx), false);
            return 0;
        };

        if (sendRedundant)
        {
            bool   sentSuccessfully{false};
            int    dropCode{0};
            for (std::size_t i = 0; i < pd.pubChannels.size(); ++i)
            {
                auto& ch = pd.pubChannels[i];
                int   rc = sendOnce(ch, i);
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
            if (sentSuccessfully)
                return 0;
            if (dropCode)
                return dropCode;
            return -1;
        }
        else
        {
            int rc = sendOnce(pd.pubChannels.at(idx), idx);
            if (rc == kPdSoftDropCode)
                return rc;
            if (rc != 0)
                return rc;
            pd.activeChannel = static_cast<uint32_t>((idx + 1) % pd.pubChannels.size());
        }

        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "PD", "PD packet transmitted",
                                   buildPcapEventJson(pd.cfg->comId, payload.size(), "tx"));
        }
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
            return;
        }

        for (auto& pdPtr : m_ctx.pdTelegrams)
        {
            if (!pdPtr || !pdPtr->cfg || pdPtr->cfg->comId != comId)
                continue;

            std::lock_guard<std::mutex> lk(pdPtr->mtx);
            pdPtr->stats.rxCount++;
            pdPtr->stats.lastRxTime = std::chrono::steady_clock::now();
            (void) data;
            (void) len;
        }
    }

    int TrdpAdapter::sendMdRequest(engine::md::MdSessionRuntime& session, const std::vector<uint8_t>& payload)
    {
        if (!m_ctx.trdpSession || !session.telegram)
            return -1;

        {
            std::lock_guard<std::mutex> lk(m_errMtx);
            m_requestedSessions.push_back(session.sessionId);
            m_lastMdRequestPayload = payload;
        }

        TRDP_IP_ADDR_T destIp = 0;
        if (!session.telegram->destinations.empty())
            destIp = toIp(session.telegram->destinations.front().uri);

        TRDP_URI_USER_T srcUri{};
        TRDP_URI_USER_T destUri{};
        TRDP_ERR_T      err = tlm_request(
            m_ctx.trdpSession, this, &mdCallback, &session.trdpSessionId, session.telegram->comId, 0, 0, 0, destIp, 0, 0,
            0, nullptr, const_cast<UINT8*>(payload.empty() ? nullptr : payload.data()),
            static_cast<UINT32>(payload.size()), srcUri, destUri);

        if (err != TRDP_NO_ERR)
        {
            recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::mdRequestErrors);
            std::cerr << "tlm_request failed for session " << session.sessionId << " error=" << err << std::endl;
            if (m_ctx.diagManager)
                m_ctx.diagManager->log(diag::Severity::ERROR, "MD", "MD request failed",
                                       buildPcapEventJson(session.comId, payload.size(), "tx"));
            return -static_cast<int>(err);
        }
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD request sent",
                                   buildPcapEventJson(session.comId, payload.size(), "tx"));
        }
        return 0;
    }

    int TrdpAdapter::sendMdReply(engine::md::MdSessionRuntime& session, const std::vector<uint8_t>& payload)
    {
        if (!m_ctx.trdpSession || !session.telegram)
            return -1;

        {
            std::lock_guard<std::mutex> lk(m_errMtx);
            m_repliedSessions.push_back(session.sessionId);
            m_lastMdReplyPayload = payload;
        }

        TRDP_ERR_T err = tlm_reply(
            m_ctx.trdpSession, &session.trdpSessionId, session.telegram->comId, 0, nullptr,
            const_cast<UINT8*>(payload.empty() ? nullptr : payload.data()), static_cast<UINT32>(payload.size()), nullptr);

        if (err != TRDP_NO_ERR)
        {
            recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::mdReplyErrors);
            std::cerr << "tlm_reply failed for session " << session.sessionId << " error=" << err << std::endl;
            if (m_ctx.diagManager)
                m_ctx.diagManager->log(diag::Severity::ERROR, "MD", "MD reply failed",
                                       buildPcapEventJson(session.comId, payload.size(), "tx"));
            return -static_cast<int>(err);
        }
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD reply sent",
                                   buildPcapEventJson(session.comId, payload.size(), "tx"));
        }
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
            return;
        }

        TRDP_UUID_T trdpSessionId{};
        if (info)
            std::memcpy(&trdpSessionId, &info->sessionId, sizeof(TRDP_UUID_T));

        engine::md::MdSessionRuntime* sess = nullptr;
        for (auto& [_, candidate] : m_ctx.mdSessions)
        {
            if (candidate && std::memcmp(&candidate->trdpSessionId, &trdpSessionId, sizeof(TRDP_UUID_T)) == 0)
            {
                sess = candidate.get();
                break;
            }
        }
        if (!sess)
            return;
        std::lock_guard<std::mutex> lk(sess->mtx);
        sess->state = engine::md::MdSessionState::REPLY_RECEIVED;
        (void) data;
        (void) len;
    }

    void TrdpAdapter::processOnce()
    {
        if (!m_ctx.trdpSession)
            return;

        TRDP_FDS_T  rfds;
        TRDP_TIME_T interval;
        INT32       noOfDesc = 0;

        FD_ZERO(&rfds);

        if (tlc_getInterval(m_ctx.trdpSession, &interval, &rfds, &noOfDesc) != TRDP_NO_ERR)
        {
            recordError(0, &TrdpErrorCounters::eventLoopErrors);
            return;
        }

        struct timeval tv;
        tv.tv_sec  = interval.tv_sec;
        tv.tv_usec = interval.tv_usec;

        int rv = select(noOfDesc + 1, &rfds, nullptr, nullptr, &tv);
        if (rv < 0)
        {
            std::perror("select");
            recordError(errno, &TrdpErrorCounters::eventLoopErrors);
            return;
        }

        auto err = tlc_process(m_ctx.trdpSession, &rfds, nullptr);
        if (err != TRDP_NO_ERR)
            recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::eventLoopErrors);
    }

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

    std::vector<PdSendLogEntry> TrdpAdapter::getPdSendLog() const
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        return m_pdSendLog;
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

} // namespace trdp_sim::trdp
