#include "md_engine.hpp"
#include "trdp_adapter.hpp"

#include <iostream>

namespace engine::md {

MdEngine::MdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter)
    : m_ctx(ctx)
    , m_adapter(adapter)
{
}

void MdEngine::initializeFromConfig()
{
    m_telegramByComId.clear();
    for (const auto& iface : m_ctx.deviceConfig.interfaces) {
        for (const auto& tel : iface.telegrams) {
            if (tel.pdParam)
                continue; // PD telegrams handled by PdEngine
            m_telegramByComId[tel.comId] = MdTelegramBinding{ &tel, &iface };
        }
    }
}

void MdEngine::start()
{
    m_running.store(true);
    // TODO: optionally start worker thread if needed
}

void MdEngine::stop()
{
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.join();
}

uint32_t MdEngine::createRequestSession(uint32_t comId)
{
    std::lock_guard<std::mutex> lock(m_sessionsMtx);
    uint32_t id = m_nextSessionId++;

    auto it = m_telegramByComId.find(comId);
    if (it == m_telegramByComId.end()) {
        std::cerr << "MD telegram not found for COM ID " << comId << std::endl;
        return 0;
    }

    auto dsIt = m_ctx.dataSetInstances.find(it->second.telegram->dataSetId);
    if (dsIt == m_ctx.dataSetInstances.end()) {
        std::cerr << "Dataset instance missing for MD COM ID " << comId << std::endl;
        return 0;
    }

    auto* sess = new MdSessionRuntime();
    sess->sessionId = id;
    sess->telegram = it->second.telegram;
    sess->requestData = dsIt->second.get();
    sess->responseData = dsIt->second.get();
    sess->proto = it->second.iface && it->second.iface->mdCom.protocol == config::MdComParameter::Protocol::TCP
        ? MdProtocol::TCP
        : MdProtocol::UDP;
    m_ctx.mdSessions[id].reset(sess);
    return id;
}

void MdEngine::sendRequest(uint32_t sessionId)
{
    auto opt = getSession(sessionId);
    if (!opt)
        return;
    MdSessionRuntime* sess = *opt;
    // TODO: marshal requestData and call m_adapter.sendMdRequest(*sess)
    sess->state = MdSessionState::REQUEST_SENT;
}

void MdEngine::onMdIndication(uint32_t sessionId, const uint8_t* data, std::size_t len)
{
    (void)len;
    auto opt = getSession(sessionId);
    if (!opt)
        return;
    MdSessionRuntime* sess = *opt;
    std::lock_guard<std::mutex> lk(sess->mtx);
    // TODO: unmarshal into responseData
    sess->state = MdSessionState::REPLY_RECEIVED;
}

std::optional<MdSessionRuntime*> MdEngine::getSession(uint32_t sessionId)
{
    std::lock_guard<std::mutex> lock(m_sessionsMtx);
    auto it = m_ctx.mdSessions.find(sessionId);
    if (it == m_ctx.mdSessions.end())
        return std::nullopt;
    return it->second.get();
}

} // namespace engine::md
