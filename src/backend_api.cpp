#include "backend_api.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

namespace api {

BackendApi::BackendApi(trdp_sim::EngineContext& ctx,
                       engine::pd::PdEngine& pd,
                       engine::md::MdEngine& md,
                       diag::DiagnosticManager& diag)
    : m_ctx(ctx)
    , m_pd(pd)
    , m_md(md)
    , m_diag(diag)
{
}

nlohmann::json BackendApi::getPdStatus() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& telPtr : m_ctx.pdTelegrams) {
        if (!telPtr || !telPtr->cfg)
            continue;
        nlohmann::json item;
        std::lock_guard<std::mutex> lk(telPtr->mtx);
        item["name"] = telPtr->cfg->name;
        item["comId"] = telPtr->cfg->comId;
        item["dataSetId"] = telPtr->cfg->dataSetId;
        item["direction"] = telPtr->direction == engine::pd::Direction::PUBLISH ? "PUBLISH" : "SUBSCRIBE";
        item["enabled"] = telPtr->enabled;
        item["locked"] = telPtr->dataset ? telPtr->dataset->locked : false;
        item["redundantActive"] = telPtr->redundantActive;
        item["activeChannel"] = telPtr->activeChannel;
        item["stats"]["txCount"] = telPtr->stats.txCount;
        item["stats"]["rxCount"] = telPtr->stats.rxCount;
        item["stats"]["timeoutCount"] = telPtr->stats.timeoutCount;
        item["stats"]["lastSeqNumber"] = telPtr->stats.lastSeqNumber;
        item["stats"]["lastTxTime"] = telPtr->stats.lastTxTime.time_since_epoch().count();
        item["stats"]["lastRxTime"] = telPtr->stats.lastRxTime.time_since_epoch().count();
        item["stats"]["lastCycleJitterUs"] = telPtr->stats.lastCycleJitterUs;
        arr.push_back(std::move(item));
    }
    return arr;
}

void BackendApi::enablePdTelegram(uint32_t comId, bool enable)
{
    m_pd.enableTelegram(comId, enable);
}

nlohmann::json BackendApi::getDataSetValues(uint32_t dataSetId) const
{
    nlohmann::json j;
    auto it = m_ctx.dataSetInstances.find(dataSetId);
    if (it == m_ctx.dataSetInstances.end())
        return j;

    auto* inst = it->second.get();
    std::lock_guard<std::mutex> lock(inst->mtx);

    static const auto bytesToHex = [](const std::vector<uint8_t>& data) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto b : data)
            oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    };

    auto elementTypeToString = [](data::ElementType t) {
        switch (t) {
        case data::ElementType::BOOL8: return "BOOL8";
        case data::ElementType::CHAR8: return "CHAR8";
        case data::ElementType::UTF16: return "UTF16";
        case data::ElementType::INT8: return "INT8";
        case data::ElementType::INT16: return "INT16";
        case data::ElementType::INT32: return "INT32";
        case data::ElementType::INT64: return "INT64";
        case data::ElementType::UINT8: return "UINT8";
        case data::ElementType::UINT16: return "UINT16";
        case data::ElementType::UINT32: return "UINT32";
        case data::ElementType::UINT64: return "UINT64";
        case data::ElementType::REAL32: return "REAL32";
        case data::ElementType::REAL64: return "REAL64";
        case data::ElementType::TIMEDATE32: return "TIMEDATE32";
        case data::ElementType::TIMEDATE48: return "TIMEDATE48";
        case data::ElementType::TIMEDATE64: return "TIMEDATE64";
        case data::ElementType::NESTED_DATASET: return "NESTED_DATASET";
        }
        return "UNKNOWN";
    };

    j["dataSetId"] = dataSetId;
    j["name"] = inst->def ? inst->def->name : "";
    j["locked"] = inst->locked;
    j["isOutgoing"] = inst->isOutgoing;
    j["values"] = nlohmann::json::array();

    if (inst->def) {
        for (std::size_t idx = 0; idx < inst->def->elements.size() && idx < inst->values.size(); ++idx) {
            nlohmann::json cell;
            const auto& def = inst->def->elements[idx];
            const auto& val = inst->values[idx];
            cell["name"] = def.name;
            cell["type"] = elementTypeToString(def.type);
            cell["arraySize"] = def.arraySize;
            if (def.nestedDataSetId)
                cell["nestedDataSetId"] = *def.nestedDataSetId;
            cell["defined"] = val.defined;
            cell["raw"] = val.raw;
            cell["rawHex"] = bytesToHex(val.raw);
            j["values"].push_back(std::move(cell));
        }
    }
    return j;
}

void BackendApi::setDataSetValue(uint32_t dataSetId, std::size_t elementIdx,
                                 const std::vector<uint8_t>& value)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> lock(inst->mtx);
    if (elementIdx >= inst->values.size())
        return;
    if (inst->locked)
        return;

    inst->values[elementIdx].raw = value;
    inst->values[elementIdx].defined = true;
}

void BackendApi::clearDataSetValue(uint32_t dataSetId, std::size_t elementIdx)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> lock(inst->mtx);
    if (elementIdx >= inst->values.size())
        return;
    if (inst->locked)
        return;

    inst->values[elementIdx].raw.clear();
    inst->values[elementIdx].defined = false;
}

void BackendApi::clearAllDataSetValues(uint32_t dataSetId)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> lock(inst->mtx);
    if (inst->locked)
        return;
    for (auto& cell : inst->values) {
        cell.raw.clear();
        cell.defined = false;
    }
}

void BackendApi::lockDataSet(uint32_t dataSetId, bool lock)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> guard(inst->mtx);
    inst->locked = lock;
}

uint32_t BackendApi::createMdRequest(uint32_t comId)
{
    return m_md.createRequestSession(comId);
}

void BackendApi::sendMdRequest(uint32_t sessionId)
{
    m_md.sendRequest(sessionId);
}

nlohmann::json BackendApi::getMdSessionStatus(uint32_t sessionId) const
{
    nlohmann::json j;
    auto opt = m_md.getSession(sessionId);
    if (!opt)
        return j;

    auto* sess = *opt;
    std::lock_guard<std::mutex> lock(sess->mtx);
    j["sessionId"] = sess->sessionId;
    j["comId"] = sess->comId;
    j["role"] = sess->role == engine::md::MdRole::REQUESTER ? "REQUESTER" : "RESPONDER";
    j["state"] = engine::md::MdEngine::stateToString(sess->state);
    j["retryCount"] = sess->retryCount;
    j["stats"]["txCount"] = sess->stats.txCount;
    j["stats"]["rxCount"] = sess->stats.rxCount;
    j["stats"]["retryCount"] = sess->stats.retryCount;
    j["stats"]["timeoutCount"] = sess->stats.timeoutCount;
    j["stats"]["lastTxTime"] = sess->stats.lastTxTime.time_since_epoch().count();
    j["stats"]["lastRxTime"] = sess->stats.lastRxTime.time_since_epoch().count();
    return j;
}

void BackendApi::reloadConfiguration(const std::string& xmlPath)
{
    config::ConfigManager cfgMgr;
    auto cfg = cfgMgr.loadDeviceConfigFromXml(xmlPath);
    cfgMgr.validateDeviceConfig(cfg);

    m_pd.stop();
    m_md.stop();

    m_ctx.pdTelegrams.clear();
    m_ctx.mdSessions.clear();

    m_ctx.deviceConfig = cfg;

    std::unordered_map<uint32_t, data::DataSetDef> newDefs;
    std::unordered_map<uint32_t, std::unique_ptr<data::DataSetInstance>> newInsts;
    auto defs = cfgMgr.buildDataSetDefs(m_ctx.deviceConfig);
    for (auto& def : defs) {
        newDefs[def.id] = def;
        auto inst = std::make_unique<data::DataSetInstance>();
        inst->def = &newDefs[def.id];
        inst->values.resize(def.elements.size());
        newInsts[def.id] = std::move(inst);
    }
    m_ctx.dataSetDefs = std::move(newDefs);
    m_ctx.dataSetInstances = std::move(newInsts);

    m_pd.initializeFromConfig();
    m_md.initializeFromConfig();

    m_pd.start();
    m_md.start();
}

nlohmann::json BackendApi::getConfigSummary() const
{
    nlohmann::json j;
    j["hostName"] = m_ctx.deviceConfig.hostName;
    j["leaderName"] = m_ctx.deviceConfig.leaderName;
    j["interfaces"] = m_ctx.deviceConfig.interfaces.size();
    j["dataSets"] = m_ctx.deviceConfig.dataSets.size();

    std::size_t pdCount = 0, mdCount = 0;
    for (const auto& iface : m_ctx.deviceConfig.interfaces) {
        for (const auto& tel : iface.telegrams) {
            if (tel.pdParam)
                pdCount++;
            else
                mdCount++;
        }
    }

    j["pdTelegrams"] = pdCount;
    j["mdTelegrams"] = mdCount;
    j["runtime"]["activePdTelegrams"] = m_ctx.pdTelegrams.size();
    j["runtime"]["activeMdSessions"] = m_ctx.mdSessions.size();
    return j;
}

nlohmann::json BackendApi::getRecentEvents(std::size_t maxEvents) const
{
    nlohmann::json j = nlohmann::json::array();
    auto events = m_diag.fetchRecent(maxEvents);
    for (const auto& ev : events) {
        nlohmann::json item;
        item["component"] = ev.component;
        item["message"] = ev.message;
        item["severity"] = [sev = ev.severity]() {
            switch (sev) {
            case diag::Severity::DEBUG: return "DEBUG";
            case diag::Severity::INFO: return "INFO";
            case diag::Severity::WARN: return "WARN";
            case diag::Severity::ERROR: return "ERROR";
            case diag::Severity::FATAL: return "FATAL";
            }
            return "UNKNOWN";
        }();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(ev.timestamp.time_since_epoch()).count();
        item["timestampMs"] = ts;
        if (ev.extraJson)
            item["extra"] = *ev.extraJson;
        j.push_back(std::move(item));
    }
    return j;
}

void BackendApi::triggerDiagnosticEvent(const std::string& severity,
                                        const std::string& component,
                                        const std::string& message,
                                        const std::optional<std::string>& extraJson)
{
    auto sev = diag::Severity::INFO;
    auto upper = severity;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "DEBUG")
        sev = diag::Severity::DEBUG;
    else if (upper == "WARN" || upper == "WARNING")
        sev = diag::Severity::WARN;
    else if (upper == "ERROR")
        sev = diag::Severity::ERROR;
    else if (upper == "FATAL")
        sev = diag::Severity::FATAL;
    m_diag.log(sev, component, message, extraJson);
}

void BackendApi::enablePcap(bool enable)
{
    m_diag.enablePcapCapture(enable);
}

} // namespace api
