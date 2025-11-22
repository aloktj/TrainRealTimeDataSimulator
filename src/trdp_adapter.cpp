#include "trdp_adapter.hpp"
// TODO: include TRDP stack headers

namespace trdp_sim::trdp {

TrdpAdapter::TrdpAdapter(EngineContext& ctx)
    : m_ctx(ctx)
{
}

bool TrdpAdapter::init()
{
    // TODO: call TRDP init functions, configure session, memory, etc.
    return true;
}

void TrdpAdapter::deinit()
{
    // TODO: TRDP deinit
}

int TrdpAdapter::publishPd(const engine::pd::PdTelegramRuntime& pd)
{
    (void)pd;
    // TODO: marshal dataset and call TRDP PD publish API
    return 0;
}

int TrdpAdapter::subscribePd(engine::pd::PdTelegramRuntime& pd)
{
    (void)pd;
    // TODO: subscribe via TRDP
    return 0;
}

void TrdpAdapter::handlePdCallback(uint32_t comId, const uint8_t* data, std::size_t len)
{
    (void)comId;
    (void)data;
    (void)len;
    // TODO: find matching PdTelegramRuntime in m_ctx and update via PdEngine
}

int TrdpAdapter::sendMdRequest(engine::md::MdSessionRuntime& session)
{
    (void)session;
    // TODO: TRDP MD request
    return 0;
}

int TrdpAdapter::sendMdReply(engine::md::MdSessionRuntime& session)
{
    (void)session;
    // TODO: TRDP MD reply
    return 0;
}

void TrdpAdapter::handleMdCallback(uint32_t sessionId, const uint8_t* data, std::size_t len)
{
    (void)sessionId;
    (void)data;
    (void)len;
    // TODO: find MdSessionRuntime and update via MdEngine
}

void TrdpAdapter::processOnce()
{
    // TODO: integrate TRDP stack process / select / poll here
}

} // namespace trdp_sim::trdp
