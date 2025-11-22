#include "trdp_adapter.hpp"

#include "diagnostic_manager.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <vector>
#include <sys/select.h>

#if __has_include(<trdp_if_light.h>)
#include <trdp_if_light.h>
#elif __has_include(<trdp_if.h>)
#include <trdp_if.h>
#elif __has_include(<tau_api.h>)
#include <tau_api.h>
#endif

namespace trdp_sim::trdp {

namespace {

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

void pdCallback(void* refCon, TRDP_APP_SESSION_T /*session*/, const TRDP_PD_INFO_T* info, const UINT8* data, UINT32 dataSize)
{
    auto* adapter = static_cast<TrdpAdapter*>(refCon);
    if (adapter && info)
        adapter->handlePdCallback(info->comId, data, dataSize);
}

void mdCallback(void* refCon, TRDP_APP_SESSION_T /*session*/, const TRDP_MD_INFO_T* info, UINT8* data, UINT32 dataSize)
{
    auto* adapter = static_cast<TrdpAdapter*>(refCon);
    if (adapter && info)
        adapter->handleMdCallback(info->sessionId, data, dataSize);
}

} // namespace

TrdpAdapter::TrdpAdapter(EngineContext& ctx)
    : m_ctx(ctx)
{
}

bool TrdpAdapter::init()
{
    TRDP_MEM_CONFIG_T memCfg {};

    TRDP_PD_CONFIG_T pdCfg {};
    TRDP_MD_CONFIG_T mdCfg {};
    TRDP_PROCESS_CONFIG_T processCfg {};
    processCfg.szHostname = const_cast<char*>(m_ctx.deviceConfig.hostName.c_str());

    TRDP_ERR_T err = tlc_init(&m_ctx.trdpSession, &memCfg, nullptr, &pdCfg, &mdCfg, &processCfg);
    if (err != TRDP_NO_ERR) {
        recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::initErrors);
        std::cerr << "TRDP tlc_init failed: " << err << std::endl;
        return false;
    }

    return m_ctx.trdpSession != nullptr;
}

void TrdpAdapter::deinit()
{
    if (m_ctx.trdpSession) {
        tlc_terminate(m_ctx.trdpSession);
        m_ctx.trdpSession = nullptr;
    }
}

int TrdpAdapter::publishPd(const engine::pd::PdTelegramRuntime& pd)
{
    if (!m_ctx.trdpSession || !pd.cfg || !pd.pdComCfg)
        return -1;

    TRDP_PD_CONFIG_T pdCfg {};
    pdCfg.timeout = pd.cfg->pdParam ? pd.cfg->pdParam->timeoutUs : pd.pdComCfg->timeoutUs;
    pdCfg.toBehavior = (pd.cfg->pdParam && pd.cfg->pdParam->validityBehavior == config::PdComParameter::ValidityBehavior::ZERO)
        ? TRDP_TO_ZERO
        : TRDP_TO_KEEP_LAST_VALUE;

    TRDP_IP_ADDR_T srcIp = toIp(pd.ifaceCfg->hostIp);
    TRDP_IP_ADDR_T destIp = 0;
    if (!pd.cfg->destinations.empty())
        destIp = toIp(pd.cfg->destinations.front().uri);

    TRDP_ERR_T err = tlp_publish(
        m_ctx.trdpSession,
        const_cast<TRDP_PUB_T*>(&pd.pubHandle),
        srcIp,
        destIp,
        pd.cfg->comId,
        0,
        0,
        &pdCfg,
        nullptr,
        0);

    if (err != TRDP_NO_ERR) {
        recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::publishErrors);
        std::cerr << "tlp_publish failed for COM ID " << pd.cfg->comId << " error=" << err << std::endl;
        if (m_ctx.diagManager)
            m_ctx.diagManager->log(diag::Severity::ERROR, "PD", "Failed to publish", buildPcapEventJson(pd.cfg->comId, 0, "tx"));
        return -static_cast<int>(err);
    }
    return 0;
}

int TrdpAdapter::subscribePd(engine::pd::PdTelegramRuntime& pd)
{
    if (!m_ctx.trdpSession || !pd.cfg || !pd.pdComCfg)
        return -1;

    TRDP_PD_CONFIG_T pdCfg {};
    pdCfg.timeout = pd.cfg->pdParam ? pd.cfg->pdParam->timeoutUs : pd.pdComCfg->timeoutUs;
    pdCfg.toBehavior = (pd.cfg->pdParam && pd.cfg->pdParam->validityBehavior == config::PdComParameter::ValidityBehavior::ZERO)
        ? TRDP_TO_ZERO
        : TRDP_TO_KEEP_LAST_VALUE;

    TRDP_IP_ADDR_T srcIp = 0; // wildcard
    TRDP_IP_ADDR_T destIp = toIp(pd.ifaceCfg->hostIp);

    TRDP_ERR_T err = tlp_subscribe(
        m_ctx.trdpSession,
        &pd.subHandle,
        srcIp,
        destIp,
        pd.cfg->comId,
        0,
        0,
        &pdCfg,
        &pdCallback,
        this);

    if (err != TRDP_NO_ERR) {
        recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::subscribeErrors);
        std::cerr << "tlp_subscribe failed for COM ID " << pd.cfg->comId << " error=" << err << std::endl;
        if (m_ctx.diagManager)
            m_ctx.diagManager->log(diag::Severity::ERROR, "PD", "Failed to subscribe", buildPcapEventJson(pd.cfg->comId, 0, "rx"));
        return -static_cast<int>(err);
    }
    return 0;
}

int TrdpAdapter::sendPdData(const engine::pd::PdTelegramRuntime& pd, const std::vector<uint8_t>& payload)
{
    if (!m_ctx.trdpSession || !pd.cfg || !pd.pdComCfg)
        return -1;

    {
        std::lock_guard<std::mutex> lk(m_errMtx);
        m_lastPdPayload = payload;
    }

    if (!pd.pubHandle) {
        int rc = publishPd(pd);
        if (rc != 0)
            return rc;
    }

    TRDP_ERR_T err = tlp_put(
        m_ctx.trdpSession,
        pd.pubHandle,
        const_cast<UINT8*>(payload.empty() ? nullptr : payload.data()),
        static_cast<UINT32>(payload.size()));

    if (err != TRDP_NO_ERR) {
        recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::pdSendErrors);
        std::cerr << "tlp_put failed for COM ID " << pd.cfg->comId << " error=" << err << std::endl;
        if (m_ctx.diagManager)
            m_ctx.diagManager->log(diag::Severity::ERROR, "PD", "PD send failed", buildPcapEventJson(pd.cfg->comId, payload.size(), "tx"));
        return -static_cast<int>(err);
    }

    if (m_ctx.diagManager) {
        m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
        m_ctx.diagManager->log(diag::Severity::DEBUG, "PD", "PD packet transmitted", buildPcapEventJson(pd.cfg->comId, payload.size(), "tx"));
    }
    return 0;
}

void TrdpAdapter::handlePdCallback(uint32_t comId, const uint8_t* data, std::size_t len)
{
    if (m_ctx.diagManager) {
        m_ctx.diagManager->writePacketToPcap(data, len, false);
        m_ctx.diagManager->log(diag::Severity::DEBUG, "PD", "PD packet received", buildPcapEventJson(comId, len, "rx"));
    }

    if (m_ctx.pdEngine) {
        m_ctx.pdEngine->onPdReceived(comId, data, len);
        return;
    }

    for (auto& pdPtr : m_ctx.pdTelegrams) {
        if (!pdPtr || !pdPtr->cfg || pdPtr->cfg->comId != comId)
            continue;

        std::lock_guard<std::mutex> lk(pdPtr->mtx);
        pdPtr->stats.rxCount++;
        pdPtr->stats.lastRxTime = std::chrono::steady_clock::now();
        (void)data;
        (void)len;
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

    TRDP_ERR_T err = tlm_request(
        m_ctx.trdpSession,
        &session.mdHandle,
        nullptr,
        &mdCallback,
        this,
        destIp,
        session.telegram->comId,
        0,
        0,
        const_cast<UINT8*>(payload.empty() ? nullptr : payload.data()),
        static_cast<UINT32>(payload.size()),
        0,
        session.proto == engine::md::MdProtocol::TCP ? TRDP_MD_TCP : TRDP_MD_UDP,
        0,
        0);

    if (err != TRDP_NO_ERR) {
        recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::mdRequestErrors);
        std::cerr << "tlm_request failed for session " << session.sessionId << " error=" << err << std::endl;
        if (m_ctx.diagManager)
            m_ctx.diagManager->log(diag::Severity::ERROR, "MD", "MD request failed", buildPcapEventJson(session.comId, payload.size(), "tx"));
        return -static_cast<int>(err);
    }
    if (m_ctx.diagManager) {
        m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
        m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD request sent", buildPcapEventJson(session.comId, payload.size(), "tx"));
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

    TRDP_IP_ADDR_T destIp = 0;
    if (!session.telegram->destinations.empty())
        destIp = toIp(session.telegram->destinations.front().uri);

    TRDP_ERR_T err = tlm_reply(
        m_ctx.trdpSession,
        session.mdHandle,
        &mdCallback,
        this,
        destIp,
        session.telegram->comId,
        const_cast<UINT8*>(payload.empty() ? nullptr : payload.data()),
        static_cast<UINT32>(payload.size()),
        0,
        session.proto == engine::md::MdProtocol::TCP ? TRDP_MD_TCP : TRDP_MD_UDP,
        0,
        0);

    if (err != TRDP_NO_ERR) {
        recordError(static_cast<uint32_t>(err), &TrdpErrorCounters::mdReplyErrors);
        std::cerr << "tlm_reply failed for session " << session.sessionId << " error=" << err << std::endl;
        if (m_ctx.diagManager)
            m_ctx.diagManager->log(diag::Severity::ERROR, "MD", "MD reply failed", buildPcapEventJson(session.comId, payload.size(), "tx"));
        return -static_cast<int>(err);
    }
    if (m_ctx.diagManager) {
        m_ctx.diagManager->writePacketToPcap(payload.data(), payload.size(), true);
        m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD reply sent", buildPcapEventJson(session.comId, payload.size(), "tx"));
    }
    return 0;
}

void TrdpAdapter::handleMdCallback(uint32_t sessionId, const uint8_t* data, std::size_t len)
{
    if (m_ctx.diagManager) {
        m_ctx.diagManager->writePacketToPcap(data, len, false);
        m_ctx.diagManager->log(diag::Severity::DEBUG, "MD", "MD packet received", buildPcapEventJson(sessionId, len, "rx"));
    }

    if (m_ctx.mdEngine) {
        m_ctx.mdEngine->onMdIndication(sessionId, data, len);
        return;
    }

    auto it = m_ctx.mdSessions.find(sessionId);
    if (it == m_ctx.mdSessions.end())
        return;
    auto& sess = it->second;
    if (!sess)
        return;
    std::lock_guard<std::mutex> lk(sess->mtx);
    sess->state = engine::md::MdSessionState::REPLY_RECEIVED;
    (void)data;
    (void)len;
}

void TrdpAdapter::processOnce()
{
    if (!m_ctx.trdpSession)
        return;

    TRDP_FDS_T rfds;
    TRDP_TIME_T interval;
    INT32 noOfDesc = 0;

    FD_ZERO(&rfds);

    if (tlc_getInterval(m_ctx.trdpSession, &interval, &rfds, &noOfDesc) != TRDP_NO_ERR) {
        recordError(0, &TrdpErrorCounters::eventLoopErrors);
        return;
    }

    struct timeval tv;
    tv.tv_sec = interval.tvSec;
    tv.tv_usec = interval.tvUsec;

    int rv = select(noOfDesc + 1, &rfds, nullptr, nullptr, &tv);
    if (rv < 0) {
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

void TrdpAdapter::recordError(uint32_t code, uint64_t TrdpErrorCounters::*member)
{
    std::lock_guard<std::mutex> lk(m_errMtx);
    m_errorCounters.*member += 1;
    m_lastErrorCode = code;
}

} // namespace trdp_sim::trdp
