#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine_context.hpp"

namespace trdp_sim::trdp {

class TrdpAdapter
{
public:
    explicit TrdpAdapter(EngineContext& ctx);

    bool init();
    void deinit();

    // PD
    int publishPd(const engine::pd::PdTelegramRuntime& pd);
    int subscribePd(engine::pd::PdTelegramRuntime& pd);
    int sendPdData(const engine::pd::PdTelegramRuntime& pd, const std::vector<uint8_t>& payload);

    // Callbacks from TRDP stack (will be called by C layer)
    void handlePdCallback(uint32_t comId, const uint8_t* data, std::size_t len);

    // MD
    int sendMdRequest(engine::md::MdSessionRuntime& session);
    int sendMdReply(engine::md::MdSessionRuntime& session);
    void handleMdCallback(uint32_t sessionId, const uint8_t* data, std::size_t len);

    // Event loop integration
    void processOnce(); // e.g. called periodically by main loop

private:
    EngineContext& m_ctx;
};

} // namespace trdp_sim::trdp
