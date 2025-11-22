#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <mutex>

#include "engine_context.hpp"

namespace trdp_sim::trdp {

struct TrdpErrorCounters
{
    uint64_t initErrors {0};
    uint64_t publishErrors {0};
    uint64_t subscribeErrors {0};
    uint64_t pdSendErrors {0};
    uint64_t mdRequestErrors {0};
    uint64_t mdReplyErrors {0};
    uint64_t eventLoopErrors {0};
};

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

    TrdpErrorCounters getErrorCounters() const;
    std::optional<uint32_t> getLastErrorCode() const;

private:
    EngineContext& m_ctx;
    mutable std::mutex m_errMtx;
    TrdpErrorCounters m_errorCounters {};
    std::optional<uint32_t> m_lastErrorCode;

    void recordError(uint32_t code, uint64_t TrdpErrorCounters::*member);
};

} // namespace trdp_sim::trdp
