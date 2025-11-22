#include "diagnostic_manager.hpp"

#include <iostream>

namespace diag {

DiagnosticManager::DiagnosticManager() = default;

DiagnosticManager::~DiagnosticManager()
{
    stop();
}

void DiagnosticManager::start()
{
    if (m_running.exchange(true))
        return;
    m_thread = std::thread(&DiagnosticManager::workerThreadFn, this);
}

void DiagnosticManager::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_thread.joinable())
        m_thread.join();
}

void DiagnosticManager::log(Severity sev, const std::string& component,
                            const std::string& message,
                            const std::optional<std::string>& extraJson)
{
    Event ev;
    ev.timestamp = std::chrono::system_clock::now();
    ev.severity = sev;
    ev.component = component;
    ev.message = message;
    ev.extraJson = extraJson;

    std::lock_guard<std::mutex> lock(m_mtx);
    m_queue.push_back(std::move(ev));
}

std::vector<Event> DiagnosticManager::fetchRecent(std::size_t maxEvents)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    std::vector<Event> out;
    maxEvents = std::min(maxEvents, m_queue.size());
    auto it = m_queue.end();
    for (std::size_t i = 0; i < maxEvents; ++i) {
        --it;
        out.push_back(*it);
    }
    return out;
}

void DiagnosticManager::enablePcapCapture(bool enable)
{
    m_pcapEnabled = enable;
}

void DiagnosticManager::writePacketToPcap(const uint8_t* data, std::size_t len, bool isTx)
{
    (void)data;
    (void)len;
    (void)isTx;
    // TODO: implement PCAP writing
}

void DiagnosticManager::workerThreadFn()
{
    while (m_running.load()) {
        // Simple example: flush to stdout; later to log file
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            while (!m_queue.empty()) {
                const auto& ev = m_queue.front();
                std::cout << "[LOG] " << ev.component << ": " << ev.message << std::endl;
                m_queue.pop_front();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace diag
