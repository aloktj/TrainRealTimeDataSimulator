#include "md_engine.hpp"
#include "trdp_adapter.hpp"

namespace engine::md {

MdEngine::MdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter)
    : m_ctx(ctx)
    , m_adapter(adapter)
{
}

void MdEngine::initializeFromConfig()
{
    // TODO: register responders based on configuration if needed
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

    auto* sess = new MdSessionRuntime();
    sess->sessionId = id;
    // TODO: find telegram by comId and link
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
