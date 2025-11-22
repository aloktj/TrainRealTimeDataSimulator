#include "md_engine.hpp"
#include "data_marshalling.hpp"
#include "trdp_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

namespace engine::md
{

    using trdp_sim::util::marshalDataSet;
    using trdp_sim::util::unmarshalDataToDataSet;
    using MdSessionPtr = std::unique_ptr<MdSessionRuntime, trdp_sim::MdSessionDeleter>;

    MdEngine::MdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter)
        : m_ctx(ctx), m_adapter(adapter)
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

        for (const auto& [id, sessPtr] : m_ctx.mdSessions)
        {
            if (!sessPtr)
                continue;
            if (sessPtr->comId == comId && sessPtr->role == MdRole::REQUESTER)
                return id;
        }

        auto it = m_telegramByComId.find(comId);
        if (it == m_telegramByComId.end())
        {
            std::cerr << "MD telegram not found for COM ID " << comId << std::endl;
            return 0;
        }

        const auto* telegram = it->second.telegram;
        const auto* iface    = it->second.iface;
        if (!telegram || !iface)
            return 0;

        if (iface->mdCom.numSessions > 0)
        {
            uint32_t existingCount = 0;
            for (const auto& [_, sessPtr] : m_ctx.mdSessions)
            {
                if (sessPtr && sessPtr->comId == comId)
                    existingCount++;
            }
            if (existingCount >= iface->mdCom.numSessions)
                return 0;
        }

        auto dsIt = m_ctx.dataSetInstances.find(telegram->dataSetId);
        if (dsIt == m_ctx.dataSetInstances.end())
        {
            std::cerr << "Dataset instance missing for MD COM ID " << comId << std::endl;
            return 0;
        }

        MdSessionPtr sess(new MdSessionRuntime());
        sess->sessionId    = m_nextSessionId++;
        sess->comId        = comId;
        sess->telegram     = telegram;
        sess->iface        = iface;
        sess->mdCom        = &iface->mdCom;
        sess->role         = MdRole::REQUESTER;
        sess->requestData  = dsIt->second.get();
        sess->responseData = dsIt->second.get();
        sess->proto =
            iface->mdCom.protocol == config::MdComParameter::Protocol::TCP ? MdProtocol::TCP : MdProtocol::UDP;
        m_ctx.mdSessions[sess->sessionId] = std::move(sess);
        return m_nextSessionId - 1;
    }

    void MdEngine::sendRequest(uint32_t sessionId)
    {
        auto opt = getSession(sessionId);
        if (!opt)
            return;
        MdSessionRuntime*           sess = *opt;
        std::lock_guard<std::mutex> lk(sess->mtx);
        if (sess->role != MdRole::REQUESTER)
            return;

        sess->retryCount = 0;
        dispatchRequestLocked(*sess);
    }

    namespace
    {
        engine::md::MdProtocol toMdProto(const TRDP_MD_INFO_T* info)
        {
            if (info && info->protocol == TRDP_MD_TCP)
                return engine::md::MdProtocol::TCP;
            return engine::md::MdProtocol::UDP;
        }
    } // namespace

    void MdEngine::onMdIndication(const TRDP_MD_INFO_T* info, const uint8_t* data, std::size_t len)
    {
        (void) len;
        MdIndicationContext ctx;
        if (info)
        {
            ctx.sessionId = info->sessionId;
            ctx.comId     = info->comId;
            ctx.proto     = toMdProto(info);
            ctx.resultCode = info->resultCode;
        }

        auto opt = getSession(ctx.sessionId);
        if (!opt)
        {
            if (ctx.comId == 0)
                return;

            std::lock_guard<std::mutex> lock(m_sessionsMtx);
            auto                         it = m_telegramByComId.find(ctx.comId);
            if (it == m_telegramByComId.end())
                return;

            auto dsIt = m_ctx.dataSetInstances.find(it->second.telegram->dataSetId);
            if (dsIt == m_ctx.dataSetInstances.end())
                return;

            if (it->second.iface && it->second.iface->mdCom.numSessions > 0)
            {
                uint32_t existing = 0;
                for (const auto& [_, sessPtr] : m_ctx.mdSessions)
                {
                    if (sessPtr && sessPtr->comId == ctx.comId && sessPtr->role == MdRole::RESPONDER)
                        existing++;
                }
                if (existing >= it->second.iface->mdCom.numSessions)
                    return;
            }

            MdSessionPtr sess(new MdSessionRuntime());
            sess->sessionId    = ctx.sessionId;
            sess->comId        = ctx.comId;
            sess->telegram     = it->second.telegram;
            sess->iface        = it->second.iface;
            sess->mdCom        = it->second.iface ? &it->second.iface->mdCom : nullptr;
            sess->role         = MdRole::RESPONDER;
            sess->requestData  = dsIt->second.get();
            sess->responseData = dsIt->second.get();
            sess->proto        = ctx.proto;
            sess->state        = MdSessionState::IDLE;
            m_ctx.mdSessions[sess->sessionId] = std::move(sess);
            opt                                 = m_ctx.mdSessions.find(ctx.sessionId)->second.get();
        }
        if (!opt)
            return;
        MdSessionRuntime*           sess = *opt;
        std::lock_guard<std::mutex> lk(sess->mtx);
        auto                        now = std::chrono::steady_clock::now();
        sess->proto                      = ctx.proto;

        if (sess->role == MdRole::REQUESTER)
        {
            if (sess->responseData && data && len > 0)
            {
                std::lock_guard<std::mutex> dsLock(sess->responseData->mtx);
                if (!sess->responseData->locked)
                    unmarshalDataToDataSet(*sess->responseData, m_ctx, data, len);
            }
            sess->lastResponsePayload.assign(data, data + len);
            sess->stats.rxCount++;
            sess->stats.lastRxTime = now;
            sess->state            = MdSessionState::REPLY_RECEIVED;
            sess->retryCount       = 0;
            if (sess->stats.lastTxTime.time_since_epoch().count() != 0)
            {
                auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(now - sess->stats.lastTxTime);
                sess->stats.lastRoundTripUs = static_cast<uint64_t>(rtt.count());
            }
            sess->lastResponseWall = now;
        }
        else
        {
            if (len > 0)
            {
                sess->lastRequestPayload.assign(data, data + len);
                if (sess->requestData)
                {
                    std::lock_guard<std::mutex> dsLock(sess->requestData->mtx);
                    if (!sess->requestData->locked)
                        unmarshalDataToDataSet(*sess->requestData, m_ctx, data, len);
                }
                sess->stats.rxCount++;
                sess->stats.lastRxTime = now;
                sess->lastRequestWall  = now;
                dispatchReplyLocked(*sess);
            }
            else
            {
                sess->state      = MdSessionState::IDLE;
                sess->retryCount = 0;
            }
        }
    }

    std::optional<MdSessionRuntime*> MdEngine::getSession(uint32_t sessionId)
    {
        std::lock_guard<std::mutex> lock(m_sessionsMtx);
        auto                        it = m_ctx.mdSessions.find(sessionId);
        if (it == m_ctx.mdSessions.end())
            return std::nullopt;
        return it->second.get();
    }

    void MdEngine::forEachSession(const std::function<void(const MdSessionRuntime&)>& fn)
    {
        std::lock_guard<std::mutex> lock(m_sessionsMtx);
        for (const auto& [_, sessPtr] : m_ctx.mdSessions)
        {
            if (!sessPtr)
                continue;
            std::lock_guard<std::mutex> lk(sessPtr->mtx);
            fn(*sessPtr);
        }
    }

    void MdEngine::buildSessionsFromConfig()
    {
        for (const auto& iface : m_ctx.deviceConfig.interfaces)
        {
            for (const auto& tel : iface.telegrams)
            {
                if (tel.pdParam)
                    continue;

                m_telegramByComId[tel.comId] = MdTelegramBinding{&tel, &iface};

                auto dsIt = m_ctx.dataSetInstances.find(tel.dataSetId);
                if (dsIt == m_ctx.dataSetInstances.end())
                {
                    std::cerr << "Dataset instance missing for MD COM ID " << tel.comId << std::endl;
                    continue;
                }

                MdSessionPtr sess(new MdSessionRuntime());
                sess->sessionId    = m_nextSessionId++;
                sess->comId        = tel.comId;
                sess->telegram     = &tel;
                sess->iface        = &iface;
                sess->mdCom        = &iface.mdCom;
                sess->role         = tel.destinations.empty() ? MdRole::RESPONDER : MdRole::REQUESTER;
                sess->requestData  = dsIt->second.get();
                sess->responseData = dsIt->second.get();
                sess->proto =
                    iface.mdCom.protocol == config::MdComParameter::Protocol::TCP ? MdProtocol::TCP : MdProtocol::UDP;
                m_ctx.mdSessions[sess->sessionId] = std::move(sess);
            }
        }
    }

    void MdEngine::runLoop()
    {
        while (m_running.load())
        {
            handleTimeouts();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void MdEngine::handleTimeouts()
    {
        const auto            now = std::chrono::steady_clock::now();
        std::vector<uint32_t> retryIds;

        {
            std::lock_guard<std::mutex> lock(m_sessionsMtx);
            for (const auto& [id, sessPtr] : m_ctx.mdSessions)
            {
                if (!sessPtr)
                    continue;

                std::unique_lock<std::mutex> lk(sessPtr->mtx, std::try_to_lock);
                if (!lk.owns_lock())
                    continue;

                if ((sessPtr->state == MdSessionState::WAITING_REPLY ||
                     sessPtr->state == MdSessionState::WAITING_ACK) &&
                    sessPtr->deadline.time_since_epoch().count() == 0)
                {
                    continue;
                }

                if (sessPtr->state == MdSessionState::WAITING_REPLY && sessPtr->deadline <= now)
                {
                    if (sessPtr->mdCom && sessPtr->retryCount < sessPtr->mdCom->retries)
                    {
                        sessPtr->retryCount++;
                        sessPtr->stats.retryCount++;
                        sessPtr->deadline = now + std::chrono::microseconds(sessPtr->mdCom->replyTimeoutUs);
                        retryIds.push_back(id);
                    }
                    else
                    {
                        sessPtr->state = MdSessionState::TIMEOUT;
                        sessPtr->stats.timeoutCount++;
                    }
                }
                else if (sessPtr->state == MdSessionState::WAITING_ACK && sessPtr->deadline <= now)
                {
                    sessPtr->state = MdSessionState::TIMEOUT;
                    sessPtr->stats.timeoutCount++;
                }
            }
        }

        for (auto id : retryIds)
        {
            auto opt = getSession(id);
            if (!opt)
                continue;
            auto*                       sess = *opt;
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
        session.lastRequestPayload = payload;
        int rc = m_adapter.sendMdRequest(session, payload);
        if (rc != 0)
        {
            session.state = MdSessionState::ERROR;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        session.lastRequestWall = now;
        session.lastResponsePayload.clear();
        session.stats.txCount++;
        session.stats.lastTxTime = now;
        session.stats.lastRoundTripUs = 0;
        session.state            = MdSessionState::WAITING_REPLY;
        if (session.mdCom)
            session.deadline = now + std::chrono::microseconds(session.mdCom->replyTimeoutUs);
        session.lastStateChange = now;
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
        session.lastResponsePayload = payload;
        int rc = m_adapter.sendMdReply(session, payload);
        if (rc != 0)
        {
            session.state = MdSessionState::ERROR;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        session.lastResponseWall = now;
        session.stats.txCount++;
        session.stats.lastTxTime = now;
        session.state            = MdSessionState::WAITING_ACK;
        if (session.mdCom)
            session.deadline = now + std::chrono::microseconds(session.mdCom->confirmTimeoutUs);
        session.lastStateChange = now;
    }

    const char* MdEngine::stateToString(MdSessionState state)
    {
        switch (state)
        {
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
