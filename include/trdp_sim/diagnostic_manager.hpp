#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace diag {

enum class Severity { DEBUG, INFO, WARN, ERROR, FATAL };

struct Event {
    std::chrono::system_clock::time_point timestamp;
    Severity severity;
    std::string component;
    std::string message;
    std::optional<std::string> extraJson;
};

class DiagnosticManager
{
public:
    DiagnosticManager();
    ~DiagnosticManager();

    void start();
    void stop();

    void log(Severity sev, const std::string& component,
             const std::string& message,
             const std::optional<std::string>& extraJson = std::nullopt);

    std::vector<Event> fetchRecent(std::size_t maxEvents);

    void enablePcapCapture(bool enable);
    void writePacketToPcap(const uint8_t* data, std::size_t len, bool isTx);

private:
    void workerThreadFn();

    std::mutex m_mtx;
    std::deque<Event> m_queue;
    std::atomic<bool> m_running {false};
    std::thread m_thread;

    bool m_pcapEnabled {false};
    // TODO: PCAP file handle
};

} // namespace diag
