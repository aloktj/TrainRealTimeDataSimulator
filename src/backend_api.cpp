#include "backend_api.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

namespace api
{

    BackendApi::BackendApi(trdp_sim::EngineContext& ctx, trdp_sim::BackendEngine& backend, engine::pd::PdEngine& pd,
                           engine::md::MdEngine& md, diag::DiagnosticManager& diag)
        : m_ctx(ctx), m_pd(pd), m_md(md), m_diag(diag), m_backend(backend)
    {
    }

    nlohmann::json BackendApi::getPdStatus() const
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& telPtr : m_ctx.pdTelegrams)
        {
            if (!telPtr || !telPtr->cfg)
                continue;
            nlohmann::json              item;
            std::lock_guard<std::mutex> lk(telPtr->mtx);
            item["name"]             = telPtr->cfg->name;
            item["comId"]            = telPtr->cfg->comId;
            item["dataSetId"]        = telPtr->cfg->dataSetId;
            item["direction"]        = telPtr->direction == engine::pd::Direction::PUBLISH ? "PUBLISH" : "SUBSCRIBE";
            item["enabled"]          = telPtr->enabled;
            item["locked"]           = telPtr->dataset ? telPtr->dataset->locked : false;
            item["redundantActive"]  = telPtr->redundantActive;
            item["activeChannel"]    = telPtr->activeChannel;
            item["stats"]["txCount"] = telPtr->stats.txCount;
            item["stats"]["rxCount"] = telPtr->stats.rxCount;
            item["stats"]["timeoutCount"]      = telPtr->stats.timeoutCount;
            item["stats"]["lastSeqNumber"]     = telPtr->stats.lastSeqNumber;
            item["stats"]["lastTxTime"]        = telPtr->stats.lastTxTime.time_since_epoch().count();
            item["stats"]["lastRxTime"]        = telPtr->stats.lastRxTime.time_since_epoch().count();
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
        auto           it = m_ctx.dataSetInstances.find(dataSetId);
        if (it == m_ctx.dataSetInstances.end())
            return j;

        auto*                       inst = it->second.get();
        std::lock_guard<std::mutex> lock(inst->mtx);

        static const auto bytesToHex = [](const std::vector<uint8_t>& data)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (auto b : data)
                oss << std::setw(2) << static_cast<int>(b);
            return oss.str();
        };

        auto elementTypeToString = [](data::ElementType t)
        {
            switch (t)
            {
            case data::ElementType::BOOL8:
                return "BOOL8";
            case data::ElementType::CHAR8:
                return "CHAR8";
            case data::ElementType::UTF16:
                return "UTF16";
            case data::ElementType::INT8:
                return "INT8";
            case data::ElementType::INT16:
                return "INT16";
            case data::ElementType::INT32:
                return "INT32";
            case data::ElementType::INT64:
                return "INT64";
            case data::ElementType::UINT8:
                return "UINT8";
            case data::ElementType::UINT16:
                return "UINT16";
            case data::ElementType::UINT32:
                return "UINT32";
            case data::ElementType::UINT64:
                return "UINT64";
            case data::ElementType::REAL32:
                return "REAL32";
            case data::ElementType::REAL64:
                return "REAL64";
            case data::ElementType::TIMEDATE32:
                return "TIMEDATE32";
            case data::ElementType::TIMEDATE48:
                return "TIMEDATE48";
            case data::ElementType::TIMEDATE64:
                return "TIMEDATE64";
            case data::ElementType::NESTED_DATASET:
                return "NESTED_DATASET";
            }
            return "UNKNOWN";
        };

        j["dataSetId"]  = dataSetId;
        j["name"]       = inst->def ? inst->def->name : "";
        j["locked"]     = inst->locked;
        j["isOutgoing"] = inst->isOutgoing;
        j["values"]     = nlohmann::json::array();

        if (inst->def)
        {
            for (std::size_t idx = 0; idx < inst->def->elements.size() && idx < inst->values.size(); ++idx)
            {
                nlohmann::json cell;
                const auto&    def = inst->def->elements[idx];
                const auto&    val = inst->values[idx];
                cell["name"]       = def.name;
                cell["type"]       = elementTypeToString(def.type);
                cell["arraySize"]  = def.arraySize;
                if (def.nestedDataSetId)
                    cell["nestedDataSetId"] = *def.nestedDataSetId;
                cell["defined"] = val.defined;
                cell["raw"]     = val.raw;
                cell["rawHex"]  = bytesToHex(val.raw);
                j["values"].push_back(std::move(cell));
            }
        }
        return j;
    }

    void BackendApi::setDataSetValue(uint32_t dataSetId, std::size_t elementIdx, const std::vector<uint8_t>& value)
    {
        auto* inst = m_pd.getDataSetInstance(dataSetId);
        if (!inst)
            return;
        std::lock_guard<std::mutex> lock(inst->mtx);
        if (elementIdx >= inst->values.size())
            return;
        if (inst->locked)
            return;

        inst->values[elementIdx].raw     = value;
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
        for (auto& cell : inst->values)
        {
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
        auto           opt = m_md.getSession(sessionId);
        if (!opt)
            return j;

        auto*                       sess = *opt;
        std::lock_guard<std::mutex> lock(sess->mtx);
        j["sessionId"]             = sess->sessionId;
        j["comId"]                 = sess->comId;
        j["role"]                  = sess->role == engine::md::MdRole::REQUESTER ? "REQUESTER" : "RESPONDER";
        j["state"]                 = engine::md::MdEngine::stateToString(sess->state);
        j["retryCount"]            = sess->retryCount;
        j["protocol"]              = sess->proto == engine::md::MdProtocol::TCP ? "TCP" : "UDP";
        j["lastStateChangeNs"]     = sess->lastStateChange.time_since_epoch().count();
        j["deadlineNs"]            = sess->deadline.time_since_epoch().count();
        j["stats"]["txCount"]      = sess->stats.txCount;
        j["stats"]["rxCount"]      = sess->stats.rxCount;
        j["stats"]["retryCount"]   = sess->stats.retryCount;
        j["stats"]["timeoutCount"] = sess->stats.timeoutCount;
        j["stats"]["lastTxTime"]   = sess->stats.lastTxTime.time_since_epoch().count();
        j["stats"]["lastRxTime"]   = sess->stats.lastRxTime.time_since_epoch().count();
        j["stats"]["lastRoundTripUs"] = sess->stats.lastRoundTripUs;

        auto bytesToHex = [](const std::vector<uint8_t>& data)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (auto b : data)
                oss << std::setw(2) << static_cast<int>(b);
            return oss.str();
        };

        auto dataSetToJson = [](const data::DataSetInstance* inst)
        {
            nlohmann::json json;
            if (!inst)
                return json;
            std::lock_guard<std::mutex> lock(inst->mtx);
            json["dataSetId"]  = inst->def ? inst->def->id : 0;
            json["name"]       = inst->def ? inst->def->name : "";
            json["locked"]     = inst->locked;
            json["isOutgoing"] = inst->isOutgoing;
            json["values"]     = nlohmann::json::array();
            if (inst->def)
            {
                for (std::size_t idx = 0; idx < inst->def->elements.size() && idx < inst->values.size(); ++idx)
                {
                    nlohmann::json cell;
                    const auto&    def = inst->def->elements[idx];
                    const auto&    val = inst->values[idx];
                    cell["name"]       = def.name;
                    cell["type"]       = def.type;
                    cell["arraySize"]  = def.arraySize;
                    if (def.nestedDataSetId)
                        cell["nestedDataSetId"] = *def.nestedDataSetId;
                    cell["defined"] = val.defined;
                    cell["raw"]     = val.raw;
                    cell["rawHex"]  = bytesToHex(val.raw);
                    json["values"].push_back(std::move(cell));
                }
            }
            return json;
        };

        j["exchange"]["request"]["raw"]    = sess->lastRequestPayload;
        j["exchange"]["request"]["hex"]    = bytesToHex(sess->lastRequestPayload);
        j["exchange"]["response"]["raw"]   = sess->lastResponsePayload;
        j["exchange"]["response"]["hex"]   = bytesToHex(sess->lastResponsePayload);
        j["exchange"]["request"]["parsed"] = dataSetToJson(sess->requestData);
        j["exchange"]["response"]["parsed"] = dataSetToJson(sess->responseData);
        j["exchange"]["timing"]["requestNs"]  = sess->lastRequestWall.time_since_epoch().count();
        j["exchange"]["timing"]["responseNs"] = sess->lastResponseWall.time_since_epoch().count();
        return j;
    }

    void BackendApi::reloadConfiguration(const std::string& xmlPath)
    {
        m_backend.reloadConfiguration(xmlPath);
    }

    nlohmann::json BackendApi::getConfigSummary() const
    {
        nlohmann::json j;
        j["hostName"]   = m_ctx.deviceConfig.hostName;
        j["leaderName"] = m_ctx.deviceConfig.leaderName;
        j["interfaces"] = m_ctx.deviceConfig.interfaces.size();
        j["dataSets"]   = m_ctx.deviceConfig.dataSets.size();

        std::size_t pdCount = 0, mdCount = 0;
        for (const auto& iface : m_ctx.deviceConfig.interfaces)
        {
            for (const auto& tel : iface.telegrams)
            {
                if (tel.pdParam)
                    pdCount++;
                else
                    mdCount++;
            }
        }

        j["pdTelegrams"]                  = pdCount;
        j["mdTelegrams"]                  = mdCount;
        j["runtime"]["activePdTelegrams"] = m_ctx.pdTelegrams.size();
        j["runtime"]["activeMdSessions"]  = m_ctx.mdSessions.size();
        return j;
    }

    nlohmann::json BackendApi::getConfigDetail() const
    {
        nlohmann::json j;
        const auto&    cfg = m_ctx.deviceConfig;
        j["device"]["hostName"]   = cfg.hostName;
        j["device"]["leaderName"] = cfg.leaderName;
        j["device"]["type"]       = cfg.type;

        j["memory"]["memorySize"] = cfg.memory.memorySize;
        for (const auto& blk : cfg.memory.blocks)
        {
            j["memory"]["blocks"].push_back({{"size", blk.size}, {"preallocate", blk.preallocate}});
        }

        if (cfg.debug)
        {
            j["debug"]["fileName"] = cfg.debug->fileName;
            j["debug"]["fileSize"] = cfg.debug->fileSize;
            j["debug"]["info"]     = cfg.debug->info;
            j["debug"]["level"]    = std::string(1, cfg.debug->level);
        }

        if (cfg.pcap)
        {
            j["pcap"]["enabled"]      = cfg.pcap->enabled;
            j["pcap"]["captureTx"]    = cfg.pcap->captureTx;
            j["pcap"]["captureRx"]    = cfg.pcap->captureRx;
            j["pcap"]["fileName"]     = cfg.pcap->fileName;
            j["pcap"]["maxSizeBytes"] = cfg.pcap->maxSizeBytes;
            j["pcap"]["maxFiles"]     = cfg.pcap->maxFiles;
        }

        for (const auto& cp : cfg.comParameters)
        {
            j["comParameters"].push_back({{"id", cp.id}, {"qos", cp.qos}, {"ttl", cp.ttl}});
        }

        for (const auto& ds : cfg.dataSets)
        {
            nlohmann::json dsJson;
            dsJson["id"]   = ds.id;
            dsJson["name"] = ds.name;
            for (const auto& el : ds.elements)
            {
                dsJson["elements"].push_back(
                    {{"name", el.name}, {"type", el.type}, {"arraySize", el.arraySize}});
            }
            j["dataSets"].push_back(dsJson);
        }

        for (const auto& iface : cfg.interfaces)
        {
            nlohmann::json ifaceJson;
            ifaceJson["name"]      = iface.name;
            ifaceJson["networkId"] = iface.networkId;
            if (iface.hostIp)
                ifaceJson["hostIp"] = *iface.hostIp;
            ifaceJson["pdCom"] = {{"port", iface.pdCom.port},
                                    {"qos", iface.pdCom.qos},
                                    {"ttl", iface.pdCom.ttl},
                                    {"timeoutUs", iface.pdCom.timeoutUs}};
            ifaceJson["mdCom"] = {{"udpPort", iface.mdCom.udpPort},
                                    {"tcpPort", iface.mdCom.tcpPort},
                                    {"replyTimeoutUs", iface.mdCom.replyTimeoutUs},
                                    {"confirmTimeoutUs", iface.mdCom.confirmTimeoutUs},
                                    {"connectTimeoutUs", iface.mdCom.connectTimeoutUs},
                                    {"protocol", iface.mdCom.protocol == config::MdComParameter::Protocol::TCP ? "TCP"
                                                                                                                  : "UDP"}};

            for (const auto& tel : iface.telegrams)
            {
                nlohmann::json telJson;
                telJson["name"]           = tel.name;
                telJson["comId"]          = tel.comId;
                telJson["dataSetId"]      = tel.dataSetId;
                telJson["comParameterId"] = tel.comParameterId;
                telJson["hasPdParameters"] = static_cast<bool>(tel.pdParam);
                if (tel.pdParam)
                {
                    telJson["pd"]["cycleUs"]          = tel.pdParam->cycleUs;
                    telJson["pd"]["timeoutUs"]        = tel.pdParam->timeoutUs;
                    telJson["pd"]["validityBehavior"] = tel.pdParam->validityBehavior ==
                                                                   config::PdComParameter::ValidityBehavior::KEEP
                                                               ? "KEEP"
                                                               : "ZERO";
                    telJson["pd"]["redundant"] = tel.pdParam->redundant;
                }

                for (const auto& dst : tel.destinations)
                {
                    telJson["destinations"].push_back(
                        {{"id", dst.id}, {"uri", dst.uri}, {"name", dst.name}});
                }
                ifaceJson["telegrams"].push_back(telJson);
            }
            j["interfaces"].push_back(ifaceJson);
        }

        for (const auto& dev : cfg.mappedDevices)
        {
            nlohmann::json devJson;
            devJson["hostName"]   = dev.hostName;
            devJson["leaderName"] = dev.leaderName;
            for (const auto& iface : dev.interfaces)
            {
                nlohmann::json ifaceJson;
                ifaceJson["name"]     = iface.name;
                ifaceJson["hostIp"]   = iface.hostIp;
                ifaceJson["leaderIp"] = iface.leaderIp;
                for (const auto& tel : iface.mappedTelegrams)
                {
                    ifaceJson["telegrams"].push_back({{"name", tel.name}, {"comId", tel.comId}});
                }
                devJson["interfaces"].push_back(ifaceJson);
            }
            j["mappedDevices"].push_back(devJson);
        }

        return j;
    }

    nlohmann::json BackendApi::getRecentEvents(std::size_t maxEvents) const
    {
        nlohmann::json j      = nlohmann::json::array();
        auto           events = m_diag.fetchRecent(maxEvents);
        for (const auto& ev : events)
        {
            nlohmann::json item;
            item["component"] = ev.component;
            item["message"]   = ev.message;
            item["severity"]  = [sev = ev.severity]()
            {
                switch (sev)
                {
                case diag::Severity::DEBUG:
                    return "DEBUG";
                case diag::Severity::INFO:
                    return "INFO";
                case diag::Severity::WARN:
                    return "WARN";
                case diag::Severity::ERROR:
                    return "ERROR";
                case diag::Severity::FATAL:
                    return "FATAL";
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

    nlohmann::json BackendApi::getDiagnosticsMetrics() const
    {
        auto           m = m_diag.getMetrics();
        nlohmann::json j;
        j["timestampMs"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(m.timestamp.time_since_epoch()).count();
        j["threads"]["pd"]   = m.threads.pdThreadRunning;
        j["threads"]["md"]   = m.threads.mdThreadRunning;
        j["threads"]["diag"] = m.threads.diagThreadRunning;
        j["threads"]["trdp"] = m.threads.trdpThreadRunning;

        j["pd"]["telegrams"]        = m.pd.telegrams;
        j["pd"]["txCount"]          = m.pd.txCount;
        j["pd"]["rxCount"]          = m.pd.rxCount;
        j["pd"]["timeoutCount"]     = m.pd.timeoutCount;
        j["pd"]["maxCycleJitterUs"] = m.pd.maxCycleJitterUs;

        j["md"]["sessions"]     = m.md.sessions;
        j["md"]["txCount"]      = m.md.txCount;
        j["md"]["rxCount"]      = m.md.rxCount;
        j["md"]["retryCount"]   = m.md.retryCount;
        j["md"]["timeoutCount"] = m.md.timeoutCount;
        j["md"]["maxLatencyUs"] = m.md.maxLatencyUs;

        j["trdp"]["initErrors"]      = m.trdp.initErrors;
        j["trdp"]["publishErrors"]   = m.trdp.publishErrors;
        j["trdp"]["subscribeErrors"] = m.trdp.subscribeErrors;
        j["trdp"]["pdSendErrors"]    = m.trdp.pdSendErrors;
        j["trdp"]["mdRequestErrors"] = m.trdp.mdRequestErrors;
        j["trdp"]["mdReplyErrors"]   = m.trdp.mdReplyErrors;
        j["trdp"]["eventLoopErrors"] = m.trdp.eventLoopErrors;
        if (m.trdp.lastErrorCode)
            j["trdp"]["lastErrorCode"] = *m.trdp.lastErrorCode;
        return j;
    }

    void BackendApi::triggerDiagnosticEvent(const std::string& severity, const std::string& component,
                                            const std::string& message, const std::optional<std::string>& extraJson)
    {
        auto sev   = diag::Severity::INFO;
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
