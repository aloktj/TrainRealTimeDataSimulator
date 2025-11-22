#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include "engine_context.hpp"

namespace trdp_sim::trdp { class TrdpAdapter; }

namespace engine::md {

enum class MdRole {
    REQUESTER,
    RESPONDER
};

enum class MdProtocol {
    UDP,
    TCP
};

enum class MdSessionState {
    IDLE,
    REQUEST_SENT,
    WAITING_REPLY,
    REPLY_RECEIVED,
    TIMEOUT,
    ERROR
};

struct MdSessionRuntime {
    uint32_t sessionId {0};
    MdRole role {MdRole::REQUESTER};
    MdProtocol proto {MdProtocol::UDP};
    const config::TelegramConfig* telegram {nullptr};
    data::DataSetInstance* requestData {nullptr};
    data::DataSetInstance* responseData {nullptr};
    MdSessionState state {MdSessionState::IDLE};
    uint32_t retryCount {0};
    std::chrono::steady_clock::time_point lastStateChange {};
    std::mutex mtx;
};

class MdEngine
{
public:
    MdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter);

    void initializeFromConfig();
    void start();
    void stop();

    uint32_t createRequestSession(uint32_t comId);
    void sendRequest(uint32_t sessionId);

    // Called by TrdpAdapter:
    void onMdIndication(uint32_t sessionId, const uint8_t* data, std::size_t len);

    std::optional<MdSessionRuntime*> getSession(uint32_t sessionId);

private:
    trdp_sim::EngineContext& m_ctx;
    trdp_sim::trdp::TrdpAdapter& m_adapter;

    std::mutex m_sessionsMtx;
    uint32_t m_nextSessionId {1};
    std::atomic<bool> m_running {false};
    std::thread m_thread; // Optional MD handling loop
};

} // namespace engine::md
