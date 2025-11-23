#include "backend_api.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

#include "data_marshalling.hpp"
#include "trdp_adapter.hpp"
#include "xml_loader.hpp"

namespace api
{

    namespace
    {
        std::string elementTypeToString(data::ElementType t)
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
        case data::ElementType::TIME_DATE32:
            return "TIMEDATE32";
        case data::ElementType::TIME_DATE48:
            return "TIMEDATE48";
        case data::ElementType::TIME_DATE64:
            return "TIMEDATE64";
            case data::ElementType::NESTED_DATASET:
                return "NESTED_DATASET";
            }
            return "UNKNOWN";
        }

        nlohmann::json buildElementSchema(const data::ElementDef& def, const trdp_sim::EngineContext& ctx)
        {
            nlohmann::json schema{{"name", def.name}, {"type", elementTypeToString(def.type)},
                                  {"arraySize", def.arraySize}};
            if (def.nestedDataSetId)
            {
                schema["nestedDataSetId"] = *def.nestedDataSetId;
                auto nestedIt              = ctx.dataSetDefs.find(*def.nestedDataSetId);
                if (nestedIt != ctx.dataSetDefs.end())
                {
                    schema["children"] = nlohmann::json::array();
                    for (const auto& child : nestedIt->second.elements)
                        schema["children"].push_back(buildElementSchema(child, ctx));
                }
            }
            return schema;
        }

        std::size_t expectedElementSize(const data::DataSetInstance* inst, std::size_t elementIdx,
                                        const trdp_sim::EngineContext& ctx)
        {
            if (!inst || !inst->def || elementIdx >= inst->def->elements.size())
                return 0;
            return trdp_sim::util::elementSize(inst->def->elements[elementIdx], ctx);
        }

        std::string computeDataSetStatus(const trdp_sim::EngineContext& ctx, const data::DataSetInstance* inst)
        {
            bool active{false};
            bool found{false};
            for (const auto& pdPtr : ctx.pdTelegrams)
            {
                if (!pdPtr || pdPtr->dataset != inst)
                    continue;
                found = true;
                std::lock_guard<std::mutex> lk(pdPtr->mtx);
                if (!pdPtr->enabled)
                    continue;
                if (pdPtr->direction == engine::pd::Direction::PUBLISH)
                {
                    active = active || pdPtr->stats.txCount > 0;
                }
                else
                {
                    active = active || (pdPtr->stats.rxCount > 0 && !pdPtr->stats.timedOut);
                }
            }

            return found && active ? "Active" : "Inactive";
        }

        nlohmann::json ruleToJson(const trdp_sim::SimulationControls::InjectionRule& rule)
        {
            nlohmann::json j;
            j["corruptComId"]    = rule.corruptComId;
            j["corruptDataSet"] = rule.corruptDataSetId;
            j["seqDelta"]       = rule.seqDelta;
            j["delayMs"]        = rule.delayMs;
            j["lossRate"]       = rule.lossRate;
            return j;
        }

        std::string toIso8601(const std::chrono::system_clock::time_point& tp)
        {
            auto    tt   = std::chrono::system_clock::to_time_t(tp);
            auto    tm   = *std::gmtime(&tt);
            auto    us   = std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()) -
                         std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
            char    buf[64];
            int     len = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06ldZ", tm.tm_year + 1900,
                                     tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                                     static_cast<long>(us.count()));
            return std::string(buf, static_cast<size_t>(std::max(len, 0)));
        }
    } // namespace

    BackendApi::BackendApi(trdp_sim::EngineContext& ctx, trdp_sim::BackendEngine& backend, engine::pd::PdEngine& pd,
                           engine::md::MdEngine& md, trdp::TrdpAdapter& trdpAdapter, diag::DiagnosticManager& diag)
        : m_ctx(ctx), m_pd(pd), m_md(md), m_diag(diag), m_backend(backend), m_trdp(trdpAdapter)
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

        j["dataSetId"]  = dataSetId;
        j["name"]       = inst->def ? inst->def->name : "";
        j["locked"]     = inst->locked;
        j["isOutgoing"] = inst->isOutgoing;
        j["readOnly"]   = !inst->isOutgoing;
        j["status"]     = computeDataSetStatus(m_ctx, inst);
        j["values"]     = nlohmann::json::array();

        if (inst->def)
        {
            j["schema"] = nlohmann::json::array();
            for (const auto& def : inst->def->elements)
                j["schema"].push_back(buildElementSchema(def, m_ctx));
        }

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

    std::optional<std::size_t> BackendApi::getExpectedElementSize(uint32_t dataSetId, std::size_t elementIdx) const
    {
        auto it = m_ctx.dataSetInstances.find(dataSetId);
        if (it == m_ctx.dataSetInstances.end())
            return std::nullopt;
        return expectedElementSize(it->second.get(), elementIdx, m_ctx);
    }

    bool BackendApi::setDataSetValue(uint32_t dataSetId, std::size_t elementIdx, const std::vector<uint8_t>& value,
                                     std::string* error)
    {
        static constexpr std::size_t kMaxDataSetPayload = 65536;

        auto* inst = m_pd.getDataSetInstance(dataSetId);
        if (!inst)
        {
            if (error)
                *error = "Unknown dataset";
            return false;
        }
        std::lock_guard<std::mutex> lock(inst->mtx);
        if (elementIdx >= inst->values.size())
        {
            if (error)
                *error = "Invalid element index";
            return false;
        }
        if (!inst->isOutgoing)
        {
            if (error)
                *error = "Dataset is read-only";
            return false;
        }
        if (inst->locked)
        {
            if (error)
                *error = "Dataset is locked";
            return false;
        }
        auto expectedSize = expectedElementSize(inst, elementIdx, m_ctx);
        if (expectedSize == 0)
        {
            if (error)
                *error = "Unsupported dataset element";
            return false;
        }
        if (value.size() > kMaxDataSetPayload)
        {
            if (error)
                *error = "Value exceeds maximum allowed payload";
            return false;
        }

        if (value.size() != expectedSize)
        {
            if (error)
            {
                *error = "Value length " + std::to_string(value.size()) + " does not match expected " +
                         std::to_string(expectedSize);
            }
            return false;
        }

        inst->values[elementIdx].raw     = value;
        inst->values[elementIdx].defined = true;
        return true;
    }

    bool BackendApi::clearDataSetValue(uint32_t dataSetId, std::size_t elementIdx, std::string* error)
    {
        auto* inst = m_pd.getDataSetInstance(dataSetId);
        if (!inst)
        {
            if (error)
                *error = "Unknown dataset";
            return false;
        }
        std::lock_guard<std::mutex> lock(inst->mtx);
        if (elementIdx >= inst->values.size())
        {
            if (error)
                *error = "Invalid element index";
            return false;
        }
        if (!inst->isOutgoing)
        {
            if (error)
                *error = "Dataset is read-only";
            return false;
        }
        if (inst->locked)
        {
            if (error)
                *error = "Dataset is locked";
            return false;
        }

        inst->values[elementIdx].raw.clear();
        inst->values[elementIdx].defined = false;
        return true;
    }

    bool BackendApi::clearAllDataSetValues(uint32_t dataSetId, std::string* error)
    {
        auto* inst = m_pd.getDataSetInstance(dataSetId);
        if (!inst)
        {
            if (error)
                *error = "Unknown dataset";
            return false;
        }
        std::lock_guard<std::mutex> lock(inst->mtx);
        if (!inst->isOutgoing)
        {
            if (error)
                *error = "Dataset is read-only";
            return false;
        }
        if (inst->locked)
        {
            if (error)
                *error = "Dataset is locked";
            return false;
        }
        for (auto& cell : inst->values)
        {
            cell.raw.clear();
            cell.defined = false;
        }
        return true;
    }

    bool BackendApi::lockDataSet(uint32_t dataSetId, bool lock, std::string* error)
    {
        auto* inst = m_pd.getDataSetInstance(dataSetId);
        if (!inst)
        {
            if (error)
                *error = "Unknown dataset";
            return false;
        }
        std::lock_guard<std::mutex> guard(inst->mtx);
        inst->locked = lock;
        return true;
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

        auto dataSetToJson = [&bytesToHex](const data::DataSetInstance* inst)
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
        m_ctx.configPath = xmlPath;
    }

    bool BackendApi::startTransport()
    {
        return m_backend.startTransport();
    }

    void BackendApi::stopTransport()
    {
        m_backend.stopTransport();
    }

    nlohmann::json BackendApi::getTransportStatus() const
    {
        nlohmann::json j;
        j["active"]      = m_backend.transportActive();
        j["configPath"]  = m_ctx.configPath;
        j["interfaces"]  = nlohmann::json::array();
        j["pdTelegrams"] = nlohmann::json::array();
        j["mdSessions"]  = nlohmann::json::array();

        for (const auto& iface : m_ctx.deviceConfig.interfaces)
        {
            nlohmann::json item;
            item["name"]           = iface.name;
            item["hostIp"]         = iface.hostIp.value_or("");
            item["multicastGroups"] = nlohmann::json::array();
            for (const auto& group : iface.multicastGroups)
            {
                nlohmann::json g;
                g["address"] = group.address;
                g["nic"]     = group.nic.value_or("");
                item["multicastGroups"].push_back(g);
            }
            j["interfaces"].push_back(std::move(item));
        }

        for (const auto& telPtr : m_ctx.pdTelegrams)
        {
            if (!telPtr || !telPtr->cfg)
                continue;
            nlohmann::json item;
            item["name"]      = telPtr->cfg->name;
            item["comId"]     = telPtr->cfg->comId;
            item["dataSetId"] = telPtr->cfg->dataSetId;
            item["direction"] = telPtr->direction == engine::pd::Direction::PUBLISH ? "PUBLISH" : "SUBSCRIBE";
            j["pdTelegrams"].push_back(std::move(item));
        }

        for (const auto& [id, sessPtr] : m_ctx.mdSessions)
        {
            if (!sessPtr || !sessPtr->telegram)
                continue;
            nlohmann::json item;
            item["sessionId"] = id;
            item["comId"]     = sessPtr->comId;
            item["role"]      = sessPtr->role == engine::md::MdRole::REQUESTER ? "REQUESTER" : "RESPONDER";
            j["mdSessions"].push_back(std::move(item));
        }

        return j;
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
        j["runtime"]["transportActive"]   = m_backend.transportActive();
        j["runtime"]["activePdTelegrams"] = m_backend.transportActive() ? m_ctx.pdTelegrams.size() : 0;
        j["runtime"]["activeMdSessions"]  = m_backend.transportActive() ? m_ctx.mdSessions.size() : 0;
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
                if (el.nestedDataSetId)
                    dsJson["elements"].back()["nestedDataSetId"] = *el.nestedDataSetId;
            }
            j["dataSets"].push_back(dsJson);
        }

        for (const auto& iface : cfg.interfaces)
        {
            nlohmann::json ifaceJson;
            ifaceJson["name"]      = iface.name;
            ifaceJson["networkId"] = iface.networkId;
            if (iface.nic)
                ifaceJson["nic"] = *iface.nic;
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

            for (const auto& grp : iface.multicastGroups)
            {
                nlohmann::json grpJson{{"address", grp.address}};
                if (grp.nic)
                    grpJson["nic"] = *grp.nic;
                ifaceJson["multicast"].push_back(grpJson);
            }

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

    nlohmann::json BackendApi::getMulticastStatus() const
    {
        nlohmann::json arr = nlohmann::json::array();
        std::lock_guard<std::mutex> lk(m_ctx.multicastMtx);
        for (const auto& entry : m_ctx.multicastGroups)
        {
            nlohmann::json item{{"interface", entry.ifaceName}, {"group", entry.address}, {"joined", entry.joined}};
            if (entry.nic)
                item["nic"] = *entry.nic;
            if (entry.hostIp)
                item["hostIp"] = *entry.hostIp;
            arr.push_back(std::move(item));
        }
        return arr;
    }

    bool BackendApi::joinMulticastGroup(const std::string& ifaceName, const std::string& group,
                                        const std::optional<std::string>& nic)
    {
        std::optional<std::string> hostIp;
        std::optional<std::string> resolvedNic = nic;
        for (const auto& iface : m_ctx.deviceConfig.interfaces)
        {
            if (iface.name == ifaceName)
            {
                hostIp = iface.hostIp;
                if (!resolvedNic && iface.nic)
                    resolvedNic = iface.nic;
                break;
            }
        }
        return m_trdp.joinMulticast(ifaceName, group, resolvedNic, hostIp);
    }

    bool BackendApi::leaveMulticastGroup(const std::string& ifaceName, const std::string& group)
    {
        return m_trdp.leaveMulticast(ifaceName, group);
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

    std::string BackendApi::exportRecentEventsText(std::size_t maxEvents) const
    {
        auto events = m_diag.fetchRecent(maxEvents);
        std::ostringstream oss;
        for (auto it = events.rbegin(); it != events.rend(); ++it)
        {
            oss << m_diag.formatEventLine(*it) << '\n';
        }
        return oss.str();
    }

    bool BackendApi::exportRecentEventsToFile(std::size_t maxEvents, bool asJson,
                                              const std::filesystem::path& destination) const
    {
        try
        {
            if (!destination.parent_path().empty())
                std::filesystem::create_directories(destination.parent_path());
            std::ofstream out(destination);
            if (!out.is_open())
                return false;

            if (asJson)
                out << getRecentEvents(maxEvents).dump(2);
            else
                out << exportRecentEventsText(maxEvents);
            return true;
        }
        catch (...)
        {
            return false;
        }
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
        j["pd"]["stressBursts"]     = m.pd.stressBursts;
        j["pd"]["redundancySwitches"] = m.pd.redundancySwitches;
        j["pd"]["busFailureDrops"]    = m.pd.busFailureDrops;

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
        {
            std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
            j["simulation"]["stress"]["enabled"]          = m_ctx.simulation.stress.enabled;
            j["simulation"]["stress"]["pdCycleOverrideUs"] = m_ctx.simulation.stress.pdCycleOverrideUs;
            j["simulation"]["stress"]["pdBurstTelegrams"] = m_ctx.simulation.stress.pdBurstTelegrams;
            j["simulation"]["stress"]["mdBurst"]          = m_ctx.simulation.stress.mdBurst;
            j["simulation"]["stress"]["mdIntervalUs"]     = m_ctx.simulation.stress.mdIntervalUs;
            j["simulation"]["redundancy"]["forceSwitch"]  = m_ctx.simulation.redundancy.forceSwitch;
            j["simulation"]["redundancy"]["busFailure"]    = m_ctx.simulation.redundancy.busFailure;
            j["simulation"]["redundancy"]["failedChannel"] = m_ctx.simulation.redundancy.failedChannel;
            j["simulation"]["timeSync"]["ntpOffsetUs"] = m_ctx.simulation.timeSync.ntpOffsetUs;
            j["simulation"]["timeSync"]["ptpOffsetUs"] = m_ctx.simulation.timeSync.ptpOffsetUs;
            j["simulation"]["activeInstance"]           = m_ctx.simulation.activeInstance;
            j["simulation"]["virtualInstances"]         = nlohmann::json::array();
            for (const auto& [name, inst] : m_ctx.simulation.instances)
            {
                nlohmann::json entry;
                entry["name"]   = name;
                entry["path"]   = inst.configPath;
                entry["active"] = m_ctx.simulation.activeInstance == name;
                j["simulation"]["virtualInstances"].push_back(entry);
            }
        }
        return j;
    }

    std::optional<std::filesystem::path> BackendApi::getPcapCapturePath() const
    {
        return m_diag.pcapFilePath();
    }

    std::optional<std::filesystem::path> BackendApi::getLogFilePath() const
    {
        return m_diag.logFilePath();
    }

    std::optional<std::filesystem::path> BackendApi::getConfigPath() const
    {
        if (m_ctx.configPath.empty())
            return std::nullopt;
        return m_ctx.configPath;
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

    bool BackendApi::exportPcapCapture(const std::filesystem::path& destination) const
    {
        auto path = getPcapCapturePath();
        if (!path || !std::filesystem::exists(*path))
            return false;
        try
        {
            if (!destination.parent_path().empty())
                std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(*path, destination, std::filesystem::copy_options::overwrite_existing);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool BackendApi::backupConfiguration(const std::filesystem::path& destination) const
    {
        if (m_ctx.configPath.empty())
            return false;
        try
        {
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(m_ctx.configPath, destination,
                                       std::filesystem::copy_options::overwrite_existing);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool BackendApi::restoreConfiguration(const std::filesystem::path& source)
    {
        if (source.empty())
            return false;
        if (!std::filesystem::exists(source))
            return false;
        reloadConfiguration(source.string());
        if (!m_ctx.configPath.empty() && m_ctx.configPath != source)
        {
            try
            {
                std::filesystem::create_directories(std::filesystem::path(m_ctx.configPath).parent_path());
                std::filesystem::copy_file(source, m_ctx.configPath,
                                           std::filesystem::copy_options::overwrite_existing);
            }
            catch (...)
            {
                // ignore copy failure, configuration already applied from source
            }
        }
        return true;
    }

    nlohmann::json BackendApi::getSimulationState() const
    {
        nlohmann::json j;
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        for (const auto& [comId, rule] : m_ctx.simulation.pdRules)
            j["pdRules"].push_back({{"comId", comId}, {"rule", ruleToJson(rule)}});
        for (const auto& [comId, rule] : m_ctx.simulation.mdRules)
            j["mdRules"].push_back({{"comId", comId}, {"rule", ruleToJson(rule)}});
        for (const auto& [dsId, rule] : m_ctx.simulation.dataSetRules)
            j["dataSetRules"].push_back({{"dataSetId", dsId}, {"rule", ruleToJson(rule)}});
        j["stress"]["enabled"]          = m_ctx.simulation.stress.enabled;
        j["stress"]["pdCycleOverrideUs"] = m_ctx.simulation.stress.pdCycleOverrideUs;
        j["stress"]["pdBurstTelegrams"] = m_ctx.simulation.stress.pdBurstTelegrams;
        j["stress"]["mdBurst"]          = m_ctx.simulation.stress.mdBurst;
        j["stress"]["mdIntervalUs"]     = m_ctx.simulation.stress.mdIntervalUs;
        j["redundancy"]["forceSwitch"]  = m_ctx.simulation.redundancy.forceSwitch;
        j["redundancy"]["busFailure"]    = m_ctx.simulation.redundancy.busFailure;
        j["redundancy"]["failedChannel"] = m_ctx.simulation.redundancy.failedChannel;
        j["timeSync"]["ntpOffsetUs"]     = m_ctx.simulation.timeSync.ntpOffsetUs;
        j["timeSync"]["ptpOffsetUs"]     = m_ctx.simulation.timeSync.ptpOffsetUs;
        j["instances"]                    = nlohmann::json::array();
        for (const auto& [name, inst] : m_ctx.simulation.instances)
        {
            nlohmann::json item;
            item["name"]   = name;
            item["path"]   = inst.configPath;
            item["active"] = m_ctx.simulation.activeInstance == name;
            j["instances"].push_back(item);
        }
        return j;
    }

    void BackendApi::upsertPdInjectionRule(uint32_t comId, const trdp_sim::SimulationControls::InjectionRule& rule)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.pdRules[comId] = rule;
    }

    void BackendApi::upsertMdInjectionRule(uint32_t comId, const trdp_sim::SimulationControls::InjectionRule& rule)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.mdRules[comId] = rule;
    }

    void BackendApi::upsertDataSetInjectionRule(uint32_t dataSetId, const trdp_sim::SimulationControls::InjectionRule& rule)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.dataSetRules[dataSetId] = rule;
    }

    void BackendApi::clearInjectionRules()
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.pdRules.clear();
        m_ctx.simulation.mdRules.clear();
        m_ctx.simulation.dataSetRules.clear();
    }

    void BackendApi::setStressMode(const trdp_sim::SimulationControls::StressMode& stress)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        auto sanitized                   = stress;
        sanitized.pdBurstTelegrams       =
            std::min<std::size_t>(stress.pdBurstTelegrams,
                                   trdp_sim::SimulationControls::StressMode::kMaxBurstTelegrams);
        sanitized.mdBurst =
            std::min<std::size_t>(stress.mdBurst, trdp_sim::SimulationControls::StressMode::kMaxBurstTelegrams);
        if (sanitized.pdCycleOverrideUs > 0 &&
            sanitized.pdCycleOverrideUs < trdp_sim::SimulationControls::StressMode::kMinCycleUs)
            sanitized.pdCycleOverrideUs = trdp_sim::SimulationControls::StressMode::kMinCycleUs;
        if (sanitized.mdIntervalUs > 0 && sanitized.mdIntervalUs < trdp_sim::SimulationControls::StressMode::kMinCycleUs)
            sanitized.mdIntervalUs = trdp_sim::SimulationControls::StressMode::kMinCycleUs;
        m_ctx.simulation.stress = sanitized;
    }

    void BackendApi::setRedundancySimulation(const trdp_sim::SimulationControls::RedundancySimulation& sim)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.redundancy = sim;
    }

    void BackendApi::setTimeSyncOffsets(const trdp_sim::SimulationControls::TimeSyncOffsets& offsets)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.timeSync = offsets;
    }

    nlohmann::json BackendApi::getTimeSyncState() const
    {
        nlohmann::json j;
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        j["ntpOffsetUs"] = m_ctx.simulation.timeSync.ntpOffsetUs;
        j["ptpOffsetUs"] = m_ctx.simulation.timeSync.ptpOffsetUs;
        auto now          = std::chrono::system_clock::now();
        j["now"]["unixMs"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        j["now"]["iso"] = toIso8601(now);
        return j;
    }

    nlohmann::json BackendApi::convertTrdpTimestamp(uint64_t secondsVal, uint32_t nanosecondsVal) const
    {
        using namespace std::chrono;
        auto base = system_clock::time_point{} + seconds(secondsVal) + nanoseconds(nanosecondsVal);

        nlohmann::json j;
        j["inputSeconds"] = secondsVal;
        j["inputNanoseconds"] = nanosecondsVal;
        j["utcIso"]      = toIso8601(base);
        j["unixMs"]      = duration_cast<milliseconds>(base.time_since_epoch()).count();
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        auto ntpAdjusted = base + microseconds(m_ctx.simulation.timeSync.ntpOffsetUs);
        auto ptpAdjusted = base + microseconds(m_ctx.simulation.timeSync.ptpOffsetUs);
        j["ntpAdjustedIso"] = toIso8601(ntpAdjusted);
        j["ptpAdjustedIso"] = toIso8601(ptpAdjusted);
        j["ntpAdjustedMs"]  = duration_cast<milliseconds>(ntpAdjusted.time_since_epoch()).count();
        j["ptpAdjustedMs"]  = duration_cast<milliseconds>(ptpAdjusted.time_since_epoch()).count();
        return j;
    }

    bool BackendApi::registerVirtualInstance(const std::string& name, const std::string& path, std::string* err)
    {
        if (name.empty() || path.empty())
        {
            if (err)
                *err = "name and path are required";
            return false;
        }
        config::XmlConfigurationLoader loader;
        config::DeviceConfig           cfg;
        try
        {
            cfg = loader.load(path);
        }
        catch (const std::exception& ex)
        {
            if (err)
                *err = ex.what();
            return false;
        }

        trdp_sim::SimulationControls::VirtualInstance inst;
        inst.name       = name;
        inst.configPath = path;
        inst.config     = std::move(cfg);

        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        m_ctx.simulation.instances[name] = std::move(inst);
        return true;
    }

    bool BackendApi::activateVirtualInstance(const std::string& name, std::string* err)
    {
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        auto                        it = m_ctx.simulation.instances.find(name);
        if (it == m_ctx.simulation.instances.end())
        {
            if (err)
                *err = "unknown instance";
            return false;
        }
        m_backend.applyPreloadedConfiguration(it->second.config);
        m_ctx.configPath               = it->second.configPath;
        m_ctx.simulation.activeInstance = name;
        return true;
    }

    nlohmann::json BackendApi::listVirtualInstances() const
    {
        nlohmann::json arr = nlohmann::json::array();
        std::lock_guard<std::mutex> lk(m_ctx.simulation.mtx);
        for (const auto& [name, inst] : m_ctx.simulation.instances)
        {
            nlohmann::json j;
            j["name"]   = name;
            j["path"]   = inst.configPath;
            j["active"] = m_ctx.simulation.activeInstance == name;
            arr.push_back(j);
        }
        return arr;
    }

} // namespace api
