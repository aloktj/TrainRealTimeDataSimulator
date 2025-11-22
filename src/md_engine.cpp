#include "md_engine.hpp"
#include "trdp_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace engine::md {

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
        return 0;
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

MdEngine::MdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter)
    : m_ctx(ctx)
    , m_adapter(adapter)
{
}

void MdEngine::initializeFromConfig()
{
    std::lock_guard<std::mutex> lock(m_sessionsMtx);
    m_telegramByComId.clear();
    m_ctx.mdSessions.clear();
    m_nextSessionId = 1;
    buildSessionsFromConfig();
}

void MdEngine::start()
{
    if (m_running.exchange(true))
        return;

    m_thread = std::thread(&MdEngine::runLoop, this);
}

void MdEngine::stop()
{
    if (!m_running.exchange(false))
        return;

    if (m_thread.joinable())
        m_thread.join();
}

uint32_t MdEngine::createRequestSession(uint32_t comId)
{
    std::lock_guard<std::mutex> lock(m_sessionsMtx);

    for (const auto& [id, sessPtr] : m_ctx.mdSessions) {
        if (!sessPtr)
            continue;
        if (sessPtr->comId == comId && sessPtr->role == MdRole::REQUESTER)
            return id;
    }

    auto it = m_telegramByComId.find(comId);
    if (it == m_telegramByComId.end()) {
        std::cerr << "MD telegram not found for COM ID " << comId << std::endl;
        return 0;
    }

    const auto* telegram = it->second.telegram;
    const auto* iface = it->second.iface;
    if (!telegram || !iface)
        return 0;

    if (iface->mdCom.numSessions > 0) {
        uint32_t existingCount = 0;
        for (const auto& [_, sessPtr] : m_ctx.mdSessions) {
            if (sessPtr && sessPtr->comId == comId)
                existingCount++;
        }
        if (existingCount >= iface->mdCom.numSessions)
            return 0;
    }

    auto dsIt = m_ctx.dataSetInstances.find(telegram->dataSetId);
    if (dsIt == m_ctx.dataSetInstances.end()) {
        std::cerr << "Dataset instance missing for MD COM ID " << comId << std::endl;
        return 0;
    }

    auto sess = std::make_unique<MdSessionRuntime>();
    sess->sessionId = m_nextSessionId++;
    sess->comId = comId;
    sess->telegram = telegram;
    sess->iface = iface;
    sess->mdCom = &iface->mdCom;
    sess->role = MdRole::REQUESTER;
    sess->requestData = dsIt->second.get();
    sess->responseData = dsIt->second.get();
    sess->proto = iface->mdCom.protocol == config::MdComParameter::Protocol::TCP ? MdProtocol::TCP : MdProtocol::UDP;
    m_ctx.mdSessions[sess->sessionId] = std::move(sess);
    return m_nextSessionId - 1;
}

void MdEngine::sendRequest(uint32_t sessionId)
{
    auto opt = getSession(sessionId);
    if (!opt)
        return;
    MdSessionRuntime* sess = *opt;
    std::lock_guard<std::mutex> lk(sess->mtx);
    if (sess->role != MdRole::REQUESTER)
        return;

    sess->retryCount = 0;
    dispatchRequestLocked(*sess);
}

void MdEngine::onMdIndication(uint32_t sessionId, const uint8_t* data, std::size_t len)
{
    (void)len;
    auto opt = getSession(sessionId);
    if (!opt)
        return;
    MdSessionRuntime* sess = *opt;
    std::lock_guard<std::mutex> lk(sess->mtx);
    auto now = std::chrono::steady_clock::now();

    if (sess->role == MdRole::REQUESTER) {
        if (sess->responseData && data && len > 0) {
            std::lock_guard<std::mutex> dsLock(sess->responseData->mtx);
            if (!sess->responseData->locked)
                unmarshalDataToDataSet(*sess->responseData, m_ctx, data, len);
        }
        sess->stats.rxCount++;
        sess->stats.lastRxTime = now;
        sess->state = MdSessionState::REPLY_RECEIVED;
        sess->retryCount = 0;
    } else {
        if (len > 0) {
            if (sess->requestData) {
                std::lock_guard<std::mutex> dsLock(sess->requestData->mtx);
                if (!sess->requestData->locked)
                    unmarshalDataToDataSet(*sess->requestData, m_ctx, data, len);
            }
            sess->stats.rxCount++;
            sess->stats.lastRxTime = now;
            dispatchReplyLocked(*sess);
        } else {
            sess->state = MdSessionState::IDLE;
            sess->retryCount = 0;
        }
    }
}

std::optional<MdSessionRuntime*> MdEngine::getSession(uint32_t sessionId)
{
    std::lock_guard<std::mutex> lock(m_sessionsMtx);
    auto it = m_ctx.mdSessions.find(sessionId);
    if (it == m_ctx.mdSessions.end())
        return std::nullopt;
    return it->second.get();
}

void MdEngine::forEachSession(const std::function<void(const MdSessionRuntime&)>& fn)
{
    std::lock_guard<std::mutex> lock(m_sessionsMtx);
    for (const auto& [_, sessPtr] : m_ctx.mdSessions) {
        if (!sessPtr)
            continue;
        std::lock_guard<std::mutex> lk(sessPtr->mtx);
        fn(*sessPtr);
    }
}

void MdEngine::buildSessionsFromConfig()
{
    for (const auto& iface : m_ctx.deviceConfig.interfaces) {
        for (const auto& tel : iface.telegrams) {
            if (tel.pdParam)
                continue;

            m_telegramByComId[tel.comId] = MdTelegramBinding { &tel, &iface };

            auto dsIt = m_ctx.dataSetInstances.find(tel.dataSetId);
            if (dsIt == m_ctx.dataSetInstances.end()) {
                std::cerr << "Dataset instance missing for MD COM ID " << tel.comId << std::endl;
                continue;
            }

            auto sess = std::make_unique<MdSessionRuntime>();
            sess->sessionId = m_nextSessionId++;
            sess->comId = tel.comId;
            sess->telegram = &tel;
            sess->iface = &iface;
            sess->mdCom = &iface.mdCom;
            sess->role = tel.destinations.empty() ? MdRole::RESPONDER : MdRole::REQUESTER;
            sess->requestData = dsIt->second.get();
            sess->responseData = dsIt->second.get();
            sess->proto = iface.mdCom.protocol == config::MdComParameter::Protocol::TCP ? MdProtocol::TCP : MdProtocol::UDP;
            m_ctx.mdSessions[sess->sessionId] = std::move(sess);
        }
    }
}

void MdEngine::runLoop()
{
    while (m_running.load()) {
        handleTimeouts();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void MdEngine::handleTimeouts()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> retryIds;

    {
        std::lock_guard<std::mutex> lock(m_sessionsMtx);
        for (const auto& [id, sessPtr] : m_ctx.mdSessions) {
            if (!sessPtr)
                continue;

            std::unique_lock<std::mutex> lk(sessPtr->mtx, std::try_to_lock);
            if (!lk.owns_lock())
                continue;

            if ((sessPtr->state == MdSessionState::WAITING_REPLY || sessPtr->state == MdSessionState::WAITING_ACK)
                && sessPtr->deadline.time_since_epoch().count() == 0) {
                continue;
            }

            if (sessPtr->state == MdSessionState::WAITING_REPLY && sessPtr->deadline <= now) {
                if (sessPtr->mdCom && sessPtr->retryCount < sessPtr->mdCom->retries) {
                    sessPtr->retryCount++;
                    sessPtr->stats.retryCount++;
                    sessPtr->deadline = now + std::chrono::microseconds(sessPtr->mdCom->replyTimeoutUs);
                    retryIds.push_back(id);
                } else {
                    sessPtr->state = MdSessionState::TIMEOUT;
                    sessPtr->stats.timeoutCount++;
                }
            } else if (sessPtr->state == MdSessionState::WAITING_ACK && sessPtr->deadline <= now) {
                sessPtr->state = MdSessionState::TIMEOUT;
                sessPtr->stats.timeoutCount++;
            }
        }
    }

    for (auto id : retryIds) {
        auto opt = getSession(id);
        if (!opt)
            continue;
        auto* sess = *opt;
        std::lock_guard<std::mutex> lk(sess->mtx);
        dispatchRequestLocked(*sess);
    }
}

void MdEngine::dispatchRequestLocked(MdSessionRuntime& session)
{
    if (!session.requestData)
        return;

    std::vector<uint8_t> payload;
    {
        std::lock_guard<std::mutex> dsLock(session.requestData->mtx);
        payload = marshalDataSet(*session.requestData, m_ctx);
    }
    int rc = m_adapter.sendMdRequest(session);
    if (rc != 0) {
        session.state = MdSessionState::ERROR;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    session.stats.txCount++;
    session.stats.lastTxTime = now;
    session.state = MdSessionState::WAITING_REPLY;
    if (session.mdCom)
        session.deadline = now + std::chrono::microseconds(session.mdCom->replyTimeoutUs);
    session.lastStateChange = now;
    (void)payload;
}

void MdEngine::dispatchReplyLocked(MdSessionRuntime& session)
{
    if (!session.responseData)
        return;

    std::vector<uint8_t> payload;
    {
        std::lock_guard<std::mutex> dsLock(session.responseData->mtx);
        payload = marshalDataSet(*session.responseData, m_ctx);
    }
    int rc = m_adapter.sendMdReply(session);
    if (rc != 0) {
        session.state = MdSessionState::ERROR;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    session.stats.txCount++;
    session.stats.lastTxTime = now;
    session.state = MdSessionState::WAITING_ACK;
    if (session.mdCom)
        session.deadline = now + std::chrono::microseconds(session.mdCom->confirmTimeoutUs);
    session.lastStateChange = now;
    (void)payload;
}

const char* MdEngine::stateToString(MdSessionState state)
{
    switch (state) {
    case MdSessionState::IDLE:
        return "IDLE";
    case MdSessionState::REQUEST_SENT:
        return "REQUEST_SENT";
    case MdSessionState::WAITING_REPLY:
        return "WAITING_REPLY";
    case MdSessionState::REPLY_RECEIVED:
        return "REPLY_RECEIVED";
    case MdSessionState::WAITING_ACK:
        return "WAITING_ACK";
    case MdSessionState::TIMEOUT:
        return "TIMEOUT";
    case MdSessionState::ERROR:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace engine::md
