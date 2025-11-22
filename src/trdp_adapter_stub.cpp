#include "trdp_adapter.hpp"
#include "data_marshalling.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace trdp_sim::trdp {

TrdpAdapter::TrdpAdapter(EngineContext& ctx)
    : m_ctx(ctx)
{
}

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
    (void)pd;
    return 0;
}

int TrdpAdapter::subscribePd(engine::pd::PdTelegramRuntime& pd)
{
    (void)pd;
    return 0;
}

int TrdpAdapter::sendPdData(const engine::pd::PdTelegramRuntime& pd, const std::vector<uint8_t>& payload)
{
    std::lock_guard<std::mutex> lk(m_errMtx);
    m_lastPdPayload = payload;
    (void)pd;
    return 0;
}

void TrdpAdapter::handlePdCallback(uint32_t comId, const uint8_t* data, std::size_t len)
{
    if (m_ctx.pdEngine) {
        m_ctx.pdEngine->onPdReceived(comId, data, len);
    }
}

int TrdpAdapter::sendMdRequest(engine::md::MdSessionRuntime& session)
{
    std::lock_guard<std::mutex> lk(m_errMtx);
    m_requestedSessions.push_back(session.sessionId);
    (void)session;
    return 0;
}

int TrdpAdapter::sendMdReply(engine::md::MdSessionRuntime& session)
{
    std::lock_guard<std::mutex> lk(m_errMtx);
    m_repliedSessions.push_back(session.sessionId);
    (void)session;
    return 0;
}

void TrdpAdapter::handleMdCallback(uint32_t sessionId, const uint8_t* data, std::size_t len)
{
    if (m_ctx.mdEngine) {
        m_ctx.mdEngine->onMdIndication(sessionId, data, len);
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

void TrdpAdapter::recordError(uint32_t code, uint64_t TrdpErrorCounters::*member)
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

} // namespace trdp_sim::trdp
