#include "diagnostic_manager.hpp"

#include "engine_context.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace diag {

namespace {

constexpr std::size_t kPcapGlobalHeaderSize = sizeof(uint32_t) * 6;

Severity severityFromChar(char c)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (c) {
    case 'D': return Severity::DEBUG;
    case 'I': return Severity::INFO;
    case 'W': return Severity::WARN;
    case 'E': return Severity::ERROR;
    case 'F': return Severity::FATAL;
    default: return Severity::INFO;
    }
}

} // namespace

DiagnosticManager::DiagnosticManager(trdp_sim::EngineContext& ctx,
                                     engine::pd::PdEngine& pd,
                                     engine::md::MdEngine& md,
                                     trdp_sim::trdp::TrdpAdapter& adapter,
                                     const LogConfig& cfg,
                                     const PcapConfig& pcapCfg)
    : m_ctx(ctx)
    , m_pd(pd)
    , m_md(md)
    , m_adapter(adapter)
    , m_logCfg(cfg)
    , m_pcapCfg(pcapCfg)
{
    if (ctx.deviceConfig.debug) {
        auto dbg = *ctx.deviceConfig.debug;
        m_logCfg.minimumSeverity = severityFromChar(dbg.level);
        if (!dbg.fileName.empty()) {
            m_logCfg.filePath = dbg.fileName;
            m_logCfg.logToStdout = false;
            m_logCfg.maxFileSizeBytes = dbg.fileSize;
        }
    }

    if (m_logCfg.filePath)
        m_logPath = *m_logCfg.filePath;

    if (m_pcapCfg.filePath)
        m_pcapPath = *m_pcapCfg.filePath;

    if (m_pcapCfg.enabled)
        log(Severity::INFO, "PCAP", "Capture enabled via configuration");
}

DiagnosticManager::~DiagnosticManager()
{
    stop();
    std::lock_guard<std::mutex> lk(m_pcapMtx);
    if (m_pcapFile.is_open())
        m_pcapFile.close();
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
    if (!shouldLog(sev))
        return;

    Event ev;
    ev.timestamp = std::chrono::system_clock::now();
    ev.severity = sev;
    ev.component = component;
    ev.message = message;
    ev.extraJson = extraJson;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue.push_back(std::move(ev));
    }
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

MetricsSnapshot DiagnosticManager::getMetrics() const
{
    std::lock_guard<std::mutex> lk(m_metricsMtx);
    return m_metrics;
}

void DiagnosticManager::updateLogConfig(const LogConfig& cfg)
{
    std::lock_guard<std::mutex> lk(m_logCfgMtx);
    m_logCfg = cfg;
    if (m_logCfg.filePath)
        m_logPath = *m_logCfg.filePath;
}

void DiagnosticManager::enablePcapCapture(bool enable)
{
    std::lock_guard<std::mutex> lk(m_pcapMtx);
    m_pcapCfg.enabled = enable;
    if (!enable && m_pcapFile.is_open())
        m_pcapFile.close();

    log(enable ? Severity::INFO : Severity::WARN, "PCAP", enable ? "Capture enabled" : "Capture disabled");
}

void DiagnosticManager::updatePcapConfig(const PcapConfig& cfg)
{
    std::lock_guard<std::mutex> lk(m_pcapMtx);
    m_pcapCfg = cfg;
    if (m_pcapCfg.filePath)
        m_pcapPath = *m_pcapCfg.filePath;
    if (!m_pcapCfg.enabled && m_pcapFile.is_open())
        m_pcapFile.close();
    log(Severity::INFO, "PCAP", "Capture configuration refreshed");
}

void DiagnosticManager::writePacketToPcap(const uint8_t* data, std::size_t len, bool isTx)
{
    if (!data || len == 0)
        return;

    std::lock_guard<std::mutex> lk(m_pcapMtx);

    if (!m_pcapCfg.enabled)
        return;
    if ((isTx && !m_pcapCfg.captureTx) || (!isTx && !m_pcapCfg.captureRx))
        return;

    const auto recordSize = sizeof(uint32_t) * 4 + len;
    if (!ensurePcapFileUnlocked(recordSize))
        return;

    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) -
                 std::chrono::duration_cast<std::chrono::microseconds>(secs);

    uint32_t header[4];
    header[0] = static_cast<uint32_t>(secs.count());
    header[1] = static_cast<uint32_t>(usecs.count());
    header[2] = static_cast<uint32_t>(len);
    header[3] = static_cast<uint32_t>(len);

    m_pcapFile.write(reinterpret_cast<const char*>(header), sizeof(header));
    m_pcapFile.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    m_pcapFile.flush();
    if (!m_pcapFile.good()) {
        log(Severity::ERROR, "PCAP", "Failed to write packet to capture file");
        return;
    }
    m_pcapBytesWritten += sizeof(header) + len;
}

bool DiagnosticManager::ensurePcapFileUnlocked(std::size_t nextPacketSize)
{
    if (!m_pcapCfg.filePath || m_pcapCfg.filePath->empty()) {
        log(Severity::ERROR, "PCAP", "Capture enabled but no file path configured");
        m_pcapCfg.enabled = false;
        return false;
    }

    std::size_t currentSize = m_pcapBytesWritten;
    try {
        if (!m_pcapFile.is_open() && std::filesystem::exists(m_pcapPath))
            currentSize = std::filesystem::file_size(m_pcapPath);
    } catch (const std::exception& ex) {
        log(Severity::ERROR, "PCAP", std::string("Failed to inspect capture file: ") + ex.what());
        return false;
    }

    const auto totalSize = currentSize + nextPacketSize;
    if (m_pcapCfg.maxFileSizeBytes > 0 && totalSize > m_pcapCfg.maxFileSizeBytes)
        rotatePcapFilesUnlocked();

    if (!m_pcapFile.is_open()) {
        try {
            if (!m_pcapPath.empty()) {
                std::filesystem::create_directories(m_pcapPath.parent_path());
            }
            m_pcapFile.open(m_pcapPath, std::ios::binary | std::ios::out | std::ios::app);
            if (!m_pcapFile.good()) {
                log(Severity::ERROR, "PCAP", "Failed to open capture file");
                m_pcapCfg.enabled = false;
                return false;
            }

            auto existingSize = std::filesystem::exists(m_pcapPath) ? std::filesystem::file_size(m_pcapPath) : 0;
            if (existingSize == 0) {
                writePcapGlobalHeader();
                m_pcapBytesWritten = kPcapGlobalHeaderSize;
                log(Severity::INFO, "PCAP", "Capture file created");
            } else {
                m_pcapBytesWritten = existingSize;
            }
        } catch (const std::exception& ex) {
            log(Severity::ERROR, "PCAP", std::string("Failed to prepare capture file: ") + ex.what());
            m_pcapCfg.enabled = false;
            return false;
        }
    }

    return true;
}

void DiagnosticManager::rotatePcapFilesUnlocked()
{
    if (m_pcapFile.is_open())
        m_pcapFile.close();

    try {
        auto maxFiles = m_pcapCfg.maxFiles == 0 ? 1u : m_pcapCfg.maxFiles;
        for (std::size_t idx = maxFiles; idx > 0; --idx) {
            auto rotated = m_pcapPath;
            rotated += "." + std::to_string(idx);
            if (std::filesystem::exists(rotated))
                std::filesystem::remove(rotated);
            if (idx == 1)
                continue;
            auto prev = m_pcapPath;
            prev += "." + std::to_string(idx - 1);
            if (std::filesystem::exists(prev))
                std::filesystem::rename(prev, rotated);
        }

        if (std::filesystem::exists(m_pcapPath))
            std::filesystem::rename(m_pcapPath, m_pcapPath.string() + ".1");

        m_pcapBytesWritten = 0;
        m_pcapFile.open(m_pcapPath, std::ios::binary | std::ios::out | std::ios::trunc);
        writePcapGlobalHeader();
        m_pcapBytesWritten = kPcapGlobalHeaderSize;
        log(Severity::INFO, "PCAP", "Capture file rotated");
    } catch (const std::exception& ex) {
        log(Severity::ERROR, "PCAP", std::string("Failed to rotate capture file: ") + ex.what());
        m_pcapCfg.enabled = false;
    }
}

void DiagnosticManager::writePcapGlobalHeader()
{
    struct Header {
        uint32_t magic {0xa1b2c3d4};
        uint16_t versionMajor {2};
        uint16_t versionMinor {4};
        int32_t thisZone {0};
        uint32_t sigFigs {0};
        uint32_t snapLen {65535};
        uint32_t network {1};
    };

    Header hdr {};
    m_pcapFile.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
}

void DiagnosticManager::workerThreadFn()
{
    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            while (!m_queue.empty()) {
                const auto& ev = m_queue.front();
                persistEvent(ev);
                m_queue.pop_front();
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (m_lastPoll.time_since_epoch().count() == 0 || now - m_lastPoll >= m_pollInterval) {
            pollMetrics();
            m_lastPoll = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DiagnosticManager::rotateLogIfNeeded()
{
    std::lock_guard<std::mutex> cfgLock(m_logCfgMtx);
    if (!m_logCfg.filePath || m_logCfg.maxFileSizeBytes == 0)
        return;

    if (!std::filesystem::exists(m_logPath))
        return;

    auto size = std::filesystem::file_size(m_logPath);
    if (size < m_logCfg.maxFileSizeBytes)
        return;

    std::filesystem::path rotated = m_logPath;
    rotated += ".1";
    if (std::filesystem::exists(rotated))
        std::filesystem::remove(rotated);
    std::filesystem::rename(m_logPath, rotated);
    m_logFile.close();
    m_logFile.open(m_logPath, std::ios::out | std::ios::trunc);
}

void DiagnosticManager::persistEvent(const Event& ev)
{
    const auto line = formatTimestamp(ev.timestamp) + " [" + severityToString(ev.severity) + "] " + ev.component + ": " + ev.message;

    {
        std::lock_guard<std::mutex> cfgLock(m_logCfgMtx);
        if (m_logCfg.logToStdout)
            std::cout << line << std::endl;

        if (m_logCfg.filePath) {
            if (!m_logFile.is_open())
                m_logFile.open(*m_logCfg.filePath, std::ios::out | std::ios::app);
            m_logFile << line << std::endl;
            m_logFile.flush();
        }
    }

    rotateLogIfNeeded();
}

void DiagnosticManager::pollMetrics()
{
    MetricsSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.threads.diagThreadRunning = m_running.load();
    snapshot.threads.pdThreadRunning = m_pd.isRunning();
    snapshot.threads.mdThreadRunning = m_md.isRunning();
    snapshot.threads.trdpThreadRunning = m_ctx.running;

    for (const auto& pdPtr : m_ctx.pdTelegrams) {
        if (!pdPtr)
            continue;
        std::lock_guard<std::mutex> lk(pdPtr->mtx);
        snapshot.pd.telegrams++;
        snapshot.pd.txCount += pdPtr->stats.txCount;
        snapshot.pd.rxCount += pdPtr->stats.rxCount;
        snapshot.pd.timeoutCount += pdPtr->stats.timeoutCount;
        snapshot.pd.maxCycleJitterUs = std::max(snapshot.pd.maxCycleJitterUs, pdPtr->stats.lastCycleJitterUs);
    }

    m_md.forEachSession([&snapshot](const engine::md::MdSessionRuntime& sess) {
        snapshot.md.sessions++;
        snapshot.md.txCount += sess.stats.txCount;
        snapshot.md.rxCount += sess.stats.rxCount;
        snapshot.md.retryCount += sess.stats.retryCount;
        snapshot.md.timeoutCount += sess.stats.timeoutCount;
        if (sess.stats.lastRxTime >= sess.stats.lastTxTime && sess.stats.lastTxTime.time_since_epoch().count() != 0) {
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(sess.stats.lastRxTime - sess.stats.lastTxTime);
            snapshot.md.maxLatencyUs = std::max(snapshot.md.maxLatencyUs, static_cast<double>(latency.count()));
        }
    });

    auto trdpErrors = m_adapter.getErrorCounters();
    snapshot.trdp.initErrors = trdpErrors.initErrors;
    snapshot.trdp.publishErrors = trdpErrors.publishErrors;
    snapshot.trdp.subscribeErrors = trdpErrors.subscribeErrors;
    snapshot.trdp.pdSendErrors = trdpErrors.pdSendErrors;
    snapshot.trdp.mdRequestErrors = trdpErrors.mdRequestErrors;
    snapshot.trdp.mdReplyErrors = trdpErrors.mdReplyErrors;
    snapshot.trdp.eventLoopErrors = trdpErrors.eventLoopErrors;
    snapshot.trdp.lastErrorCode = m_adapter.getLastErrorCode();

    {
        std::lock_guard<std::mutex> lk(m_metricsMtx);
        m_metrics = snapshot;
    }

    std::ostringstream oss;
    oss << "threads(pd=" << snapshot.threads.pdThreadRunning
        << ", md=" << snapshot.threads.mdThreadRunning
        << ", diag=" << snapshot.threads.diagThreadRunning
        << ", trdp=" << snapshot.threads.trdpThreadRunning
        << ") pd(tx=" << snapshot.pd.txCount << ", rx=" << snapshot.pd.rxCount
        << ", timeout=" << snapshot.pd.timeoutCount << ", jitter(us)=" << snapshot.pd.maxCycleJitterUs
        << ") md(tx=" << snapshot.md.txCount << ", rx=" << snapshot.md.rxCount
        << ", timeout=" << snapshot.md.timeoutCount << ", retry=" << snapshot.md.retryCount
        << ", lat(us)=" << snapshot.md.maxLatencyUs
        << ") trdp(errors=" << snapshot.trdp.eventLoopErrors << ")";
    log(Severity::DEBUG, "Diagnostics", oss.str());
}

bool DiagnosticManager::shouldLog(Severity sev) const
{
    std::lock_guard<std::mutex> lk(m_logCfgMtx);
    return static_cast<int>(sev) >= static_cast<int>(m_logCfg.minimumSeverity);
}

std::string DiagnosticManager::severityToString(Severity sev) const
{
    switch (sev) {
    case Severity::DEBUG: return "DEBUG";
    case Severity::INFO: return "INFO";
    case Severity::WARN: return "WARN";
    case Severity::ERROR: return "ERROR";
    case Severity::FATAL: return "FATAL";
    }
    return "UNKNOWN";
}

std::string DiagnosticManager::formatTimestamp(const std::chrono::system_clock::time_point& tp) const
{
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmStruct {};
#if defined(_WIN32)
    localtime_s(&tmStruct, &t);
#else
    localtime_r(&t, &tmStruct);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmStruct, "%F %T");
    return oss.str();
}

} // namespace diag
