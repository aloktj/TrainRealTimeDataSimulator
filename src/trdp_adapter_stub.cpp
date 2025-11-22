#include "data_marshalling.hpp"
#include "diagnostic_manager.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace trdp_sim::trdp
{

    namespace
    {

        std::string buildPcapEventJson(uint32_t comId, std::size_t len, const std::string& dir)
        {
            return std::string("{\"comId\":") + std::to_string(comId) + ",\"bytes\":" + std::to_string(len) +
                   ",\"direction\":\"" + dir + "\"}";
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

    int TrdpAdapter::publishPd(const engine::pd::PdTelegramRuntime& pd)
    {
        (void) pd;
        return 0;
    }

    int TrdpAdapter::subscribePd(engine::pd::PdTelegramRuntime& pd)
    {
        (void) pd;
        return 0;
    }

    int TrdpAdapter::sendPdData(const engine::pd::PdTelegramRuntime& pd, const std::vector<uint8_t>& payload)
    {
        const int rc = m_pdSendResult.value_or(0);
        if (rc != 0)
        {
            recordError(static_cast<uint32_t>(-rc), &TrdpErrorCounters::pdSendErrors);
            return rc;
        }
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_lastPdPayload = payload;
        (void) pd;
        if (m_ctx.diagManager)
        {
            m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
            m_ctx.diagManager->log(diag::Severity::DEBUG, "PD", "PD packet transmitted",
                                   buildPcapEventJson(pd.cfg ? pd.cfg->comId : 0, payload.size(), "tx"));
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

    void TrdpAdapter::recordError(uint32_t code, uint64_t TrdpErrorCounters::* member)
    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_errorCounters.*member += 1;
        m_lastErrorCode = code;
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
