#include "config_manager.hpp"

#include "data_types.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <tinyxml2.h>

namespace config
{

    ConfigError::ConfigError(const std::string& filePath, int line, const std::string& message)
        : std::runtime_error([&]() {
            std::ostringstream oss;
            if (!filePath.empty())
                oss << filePath << ':';
            if (line > 0)
                oss << line << ' ';
            oss << message;
            return oss.str();
        }()),
          m_file(filePath), m_line(line)
    {
    }

    namespace
    {

        using tinyxml2::XMLElement;

        [[noreturn]] void throwError(const std::string& path, int line, const std::string& message)
        {
            throw ConfigError(path, line, message);
        }

        template <typename T>
        T parseUnsigned(const std::string& path, XMLElement* elem, const char* attr, bool required,
                        T defaultValue = {})
        {
            const char* txt = elem->Attribute(attr);
            if (!txt)
            {
                if (required)
                    throwError(path, elem->GetLineNum(), std::string("Missing required attribute '") + attr + "'");
                return defaultValue;
            }
            unsigned long long v = std::stoull(txt);
            return static_cast<T>(v);
        }

        bool parseBool(const std::string& path, XMLElement* elem, const char* attr, bool defaultValue = false)
        {
            const char* txt = elem->Attribute(attr);
            if (!txt)
                return defaultValue;
            std::string_view sv(txt);
            return sv == "1" || sv == "true" || sv == "TRUE" || sv == "True";
        }

        std::string parseString(const std::string& path, XMLElement* elem, const char* attr, bool required,
                                const std::string& defaultValue = {})
        {
            const char* txt = elem->Attribute(attr);
            if (!txt)
            {
                if (required)
                    throwError(path, elem->GetLineNum(), std::string("Missing required attribute '") + attr + "'");
                return defaultValue;
            }
            return txt;
        }

        data::ElementType parseElementType(const std::string& path, XMLElement* elem, const std::string& name)
        {
            static const std::unordered_map<std::string, data::ElementType> map = {
                {"BOOL8", data::ElementType::BOOL8},
                {"CHAR8", data::ElementType::CHAR8},
                {"UTF16", data::ElementType::UTF16},
                {"INT8", data::ElementType::INT8},
                {"INT16", data::ElementType::INT16},
                {"INT32", data::ElementType::INT32},
                {"INT64", data::ElementType::INT64},
                {"UINT8", data::ElementType::UINT8},
                {"UINT16", data::ElementType::UINT16},
                {"UINT32", data::ElementType::UINT32},
                {"UINT64", data::ElementType::UINT64},
                {"REAL32", data::ElementType::REAL32},
                {"REAL64", data::ElementType::REAL64},
                {"TIMEDATE32", data::ElementType::TIMEDATE32},
                {"TIMEDATE48", data::ElementType::TIMEDATE48},
                {"TIMEDATE64", data::ElementType::TIMEDATE64},
                {"NESTED_DATASET", data::ElementType::NESTED_DATASET},
            };

            auto it = map.find(name);
            if (it == map.end())
                throwError(path, elem->GetLineNum(), "Unsupported element type: " + name);
            return it->second;
        }

        PdComParameter::ValidityBehavior parseValidityBehavior(const std::string& path, XMLElement* elem,
                                                               const std::string& name)
        {
            if (name == "ZERO")
                return PdComParameter::ValidityBehavior::ZERO;
            if (name == "KEEP")
                return PdComParameter::ValidityBehavior::KEEP;
            throwError(path, elem->GetLineNum(), "Unknown validityBehavior: " + name);
        }

        MdComParameter::Protocol parseMdProtocol(const std::string& name)
        {
            if (name == "TCP")
                return MdComParameter::Protocol::TCP;
            return MdComParameter::Protocol::UDP;
        }

        MemoryConfig parseMemory(const std::string& path, XMLElement* memElem)
        {
            MemoryConfig mem;
            if (!memElem)
                return mem;

            mem.memorySize = parseUnsigned<uint32_t>(path, memElem, "memorySize", false, 0);
            for (auto* blk = memElem->FirstChildElement("Block"); blk; blk = blk->NextSiblingElement("Block"))
            {
                MemBlockConfig b;
                b.size        = parseUnsigned<uint32_t>(path, blk, "size", true);
                b.preallocate = parseUnsigned<uint32_t>(path, blk, "preallocate", false, 0);
                mem.blocks.push_back(b);
            }
            return mem;
        }

        std::optional<DebugConfig> parseDebug(const std::string& path, XMLElement* dbgElem)
        {
            if (!dbgElem)
                return std::nullopt;
            DebugConfig d;
            d.fileName    = parseString(path, dbgElem, "fileName", true);
            d.fileSize    = parseUnsigned<uint32_t>(path, dbgElem, "fileSize", false, 0);
            d.info        = parseString(path, dbgElem, "info", false, {});
            auto levelStr = parseString(path, dbgElem, "level", false, "W");
            d.level       = levelStr.empty() ? 'W' : levelStr[0];
            return d;
        }

        std::optional<PcapConfig> parsePcap(const std::string& path, XMLElement* pcapElem)
        {
            if (!pcapElem)
                return std::nullopt;

            PcapConfig cfg;
            cfg.enabled      = parseBool(path, pcapElem, "enabled", false);
            cfg.captureTx    = parseBool(path, pcapElem, "captureTx", true);
            cfg.captureRx    = parseBool(path, pcapElem, "captureRx", true);
            cfg.fileName     = parseString(path, pcapElem, "fileName", true);
            cfg.maxSizeBytes = parseUnsigned<uint32_t>(path, pcapElem, "maxSizeBytes", false, 0);
            cfg.maxFiles     = parseUnsigned<uint32_t>(path, pcapElem, "maxFiles", false, 2);
            return cfg;
        }

        std::vector<ComParameter> parseComParameters(const std::string& path, XMLElement* parent)
        {
            std::vector<ComParameter> out;
            if (!parent)
                return out;
            for (auto* c = parent->FirstChildElement("ComParameter"); c; c = c->NextSiblingElement("ComParameter"))
            {
                ComParameter p;
                p.id  = parseUnsigned<uint32_t>(path, c, "id", true);
                p.qos = parseUnsigned<uint8_t>(path, c, "qos", true);
                p.ttl = parseUnsigned<uint8_t>(path, c, "ttl", true);
                out.push_back(p);
            }
            return out;
        }

        SdtParameter parseSdt(const std::string& path, XMLElement* elem)
        {
            SdtParameter s;
            if (!elem)
                return s;
            s.smi1       = parseUnsigned<uint16_t>(path, elem, "smi1", true);
            s.smi2       = parseUnsigned<uint16_t>(path, elem, "smi2", true);
            s.udv        = parseUnsigned<uint8_t>(path, elem, "udv", true);
            s.rxPeriodMs = parseUnsigned<uint32_t>(path, elem, "rxPeriodMs", true);
            s.txPeriodMs = parseUnsigned<uint32_t>(path, elem, "txPeriodMs", true);
            s.nRxsafe    = parseUnsigned<uint32_t>(path, elem, "nRxsafe", true);
            s.nGrard     = parseUnsigned<uint32_t>(path, elem, "nGrard", true);
            s.cmThr      = parseUnsigned<uint32_t>(path, elem, "cmThr", true);
            return s;
        }

        DestinationConfig parseDestination(const std::string& path, XMLElement* elem)
        {
            DestinationConfig d;
            d.id   = parseUnsigned<uint32_t>(path, elem, "id", true);
            d.uri  = parseString(path, elem, "uri", true);
            d.name = parseString(path, elem, "name", false, {});
            if (auto* sdtElem = elem->FirstChildElement("Sdt"))
                d.sdt = parseSdt(path, sdtElem);
            return d;
        }

        MulticastGroupConfig parseMulticastGroup(const std::string& path, XMLElement* elem)
        {
            MulticastGroupConfig group;
            group.address = parseString(path, elem, "address", true);
            if (const char* nic = elem->Attribute("nic"))
                group.nic = nic;
            return group;
        }

        PdParameter parsePdParameters(const std::string& path, XMLElement* elem)
        {
            PdParameter p;
            p.cycleUs          = parseUnsigned<uint32_t>(path, elem, "cycleUs", true);
            p.marshall         = parseBool(path, elem, "marshall", true);
            p.timeoutUs        = parseUnsigned<uint32_t>(path, elem, "timeoutUs", true);
            p.validityBehavior =
                parseValidityBehavior(path, elem, parseString(path, elem, "validityBehavior", false, "KEEP"));
            p.redundant        = parseUnsigned<uint8_t>(path, elem, "redundant", false, 0);
            p.callback         = parseBool(path, elem, "callback", false);
            p.offsetAddress    = parseUnsigned<uint32_t>(path, elem, "offsetAddress", false, 0);
            return p;
        }

        DataElementConfig parseElement(const std::string& path, XMLElement* elem)
        {
            DataElementConfig cfg;
            cfg.name      = parseString(path, elem, "name", true);
            auto typeStr  = parseString(path, elem, "type", true);
            cfg.type      = static_cast<uint32_t>(parseElementType(path, elem, typeStr));
            cfg.arraySize = parseUnsigned<uint32_t>(path, elem, "arraySize", false, 1);
            if (static_cast<data::ElementType>(cfg.type) == data::ElementType::NESTED_DATASET)
            {
                cfg.nestedDataSetId = parseUnsigned<uint32_t>(path, elem, "nestedDataSetId", true);
            }
            return cfg;
        }

        DataSetConfig parseDataSet(const std::string& path, XMLElement* elem)
        {
            DataSetConfig ds;
            ds.name = parseString(path, elem, "name", true);
            ds.id   = parseUnsigned<uint32_t>(path, elem, "id", true);
            for (auto* el = elem->FirstChildElement("Element"); el; el = el->NextSiblingElement("Element"))
            {
                ds.elements.push_back(parseElement(path, el));
            }
            return ds;
        }

        TrdpProcessConfig parseTrdpProcess(const std::string& path, XMLElement* elem)
        {
            TrdpProcessConfig cfg;
            if (!elem)
                return cfg;
            cfg.blocking       = parseBool(path, elem, "blocking", false);
            cfg.cycleTimeUs    = parseUnsigned<uint32_t>(path, elem, "cycleTimeUs", false, 0);
            cfg.priority       = parseUnsigned<uint8_t>(path, elem, "priority", false, 0);
            cfg.trafficShaping = parseBool(path, elem, "trafficShaping", false);
            return cfg;
        }

        PdComParameter parsePdCom(const std::string& path, XMLElement* elem)
        {
            if (!elem)
                throwError(path, 0, "Interface missing <PdCom> definition");
            PdComParameter cfg;
            cfg.marshall         = parseBool(path, elem, "marshall", true);
            cfg.port             = parseUnsigned<uint16_t>(path, elem, "port", true);
            cfg.qos              = parseUnsigned<uint8_t>(path, elem, "qos", true);
            cfg.ttl              = parseUnsigned<uint8_t>(path, elem, "ttl", true);
            cfg.timeoutUs        = parseUnsigned<uint32_t>(path, elem, "timeoutUs", true);
            cfg.validityBehavior =
                parseValidityBehavior(path, elem, parseString(path, elem, "validityBehavior", false, "KEEP"));
            cfg.callbackEnabled  = parseBool(path, elem, "callbackEnabled", false);
            return cfg;
        }

        MdComParameter parseMdCom(const std::string& path, XMLElement* elem)
        {
            if (!elem)
                throwError(path, 0, "Interface missing <MdCom> definition");
            MdComParameter cfg;
            cfg.udpPort          = parseUnsigned<uint16_t>(path, elem, "udpPort", true);
            cfg.tcpPort          = parseUnsigned<uint16_t>(path, elem, "tcpPort", true);
            cfg.confirmTimeoutUs = parseUnsigned<uint32_t>(path, elem, "confirmTimeoutUs", false, 0);
            cfg.connectTimeoutUs = parseUnsigned<uint32_t>(path, elem, "connectTimeoutUs", false, 0);
            cfg.replyTimeoutUs   = parseUnsigned<uint32_t>(path, elem, "replyTimeoutUs", false, 0);
            cfg.marshall         = parseBool(path, elem, "marshall", true);
            cfg.protocol         = parseMdProtocol(parseString(path, elem, "protocol", false, "UDP"));
            cfg.qos              = parseUnsigned<uint8_t>(path, elem, "qos", false, 0);
            cfg.ttl              = parseUnsigned<uint8_t>(path, elem, "ttl", false, 0);
            cfg.retries          = parseUnsigned<uint8_t>(path, elem, "retries", false, 0);
            cfg.numSessions      = parseUnsigned<uint32_t>(path, elem, "numSessions", false, 0);
            return cfg;
        }

        TelegramConfig parseTelegram(const std::string& path, XMLElement* elem)
        {
            TelegramConfig t;
            t.name           = parseString(path, elem, "name", true);
            t.comId          = parseUnsigned<uint32_t>(path, elem, "comId", true);
            t.dataSetId      = parseUnsigned<uint32_t>(path, elem, "dataSetId", true);
            t.comParameterId = parseUnsigned<uint32_t>(path, elem, "comParameterId", false, 0);
            if (auto* pd = elem->FirstChildElement("PdParameters"))
                t.pdParam = parsePdParameters(path, pd);

            if (auto* dests = elem->FirstChildElement("Destinations"))
            {
                for (auto* d = dests->FirstChildElement("Destination"); d; d = d->NextSiblingElement("Destination"))
                {
                    t.destinations.push_back(parseDestination(path, d));
                }
            }

            return t;
        }

        BusInterfaceConfig parseInterface(const std::string& path, XMLElement* elem)
        {
            BusInterfaceConfig iface;
            iface.networkId = parseUnsigned<uint32_t>(path, elem, "networkId", true);
            iface.name      = parseString(path, elem, "name", true);
            if (const char* nic = elem->Attribute("nic"))
                iface.nic = nic;
            if (const char* hostIp = elem->Attribute("hostIp"))
                iface.hostIp = hostIp;

            iface.trdpProcess = parseTrdpProcess(path, elem->FirstChildElement("TrdpProcess"));
            iface.pdCom       = parsePdCom(path, elem->FirstChildElement("PdCom"));
            iface.mdCom       = parseMdCom(path, elem->FirstChildElement("MdCom"));

            if (auto* mcRoot = elem->FirstChildElement("MulticastGroups"))
            {
                for (auto* grp = mcRoot->FirstChildElement("Group"); grp; grp = grp->NextSiblingElement("Group"))
                {
                    iface.multicastGroups.push_back(parseMulticastGroup(path, grp));
                }
            }

            if (auto* tRoot = elem->FirstChildElement("Telegrams"))
            {
                for (auto* t = tRoot->FirstChildElement("Telegram"); t; t = t->NextSiblingElement("Telegram"))
                {
                    iface.telegrams.push_back(parseTelegram(path, t));
                }
            }

            return iface;
        }

        MappedTelegramConfig parseMappedTelegram(const std::string& path, XMLElement* elem)
        {
            MappedTelegramConfig t;
            t.comId = parseUnsigned<uint32_t>(path, elem, "comId", true);
            t.name  = parseString(path, elem, "name", true);
            return t;
        }

        MappedBusInterfaceConfig parseMappedInterface(const std::string& path, XMLElement* elem)
        {
            MappedBusInterfaceConfig iface;
            iface.name     = parseString(path, elem, "name", true);
            iface.hostIp   = parseString(path, elem, "hostIp", true);
            iface.leaderIp = parseString(path, elem, "leaderIp", true);
            for (auto* t = elem->FirstChildElement("MappedTelegram"); t; t = t->NextSiblingElement("MappedTelegram"))
            {
                iface.mappedTelegrams.push_back(parseMappedTelegram(path, t));
            }
            return iface;
        }

        MappedDeviceConfig parseMappedDevice(const std::string& path, XMLElement* elem)
        {
            MappedDeviceConfig dev;
            dev.hostName   = parseString(path, elem, "hostName", true);
            dev.leaderName = parseString(path, elem, "leaderName", false, "");
            for (auto* iface = elem->FirstChildElement("Interface"); iface;
                 iface       = iface->NextSiblingElement("Interface"))
            {
                dev.interfaces.push_back(parseMappedInterface(path, iface));
            }
            return dev;
        }

        std::vector<SchemaIssue> validateSchemaDoc(tinyxml2::XMLDocument& doc, [[maybe_unused]] const std::string& path)
        {
            std::vector<SchemaIssue> issues;
            auto addIssue = [&issues](int line, const std::string& msg) { issues.push_back({msg, line}); };

            auto requireAttr = [&addIssue](XMLElement* elem, const char* attr)
            {
                if (!elem->Attribute(attr))
                    addIssue(elem->GetLineNum(), std::string(elem->Name()) + " missing required attribute '" + attr + "'");
            };

            auto* deviceElem = doc.FirstChildElement("Device");
            if (!deviceElem)
            {
                addIssue(doc.ErrorLineNum(), "Missing <Device> root element");
                return issues;
            }

            requireAttr(deviceElem, "hostName");

            std::unordered_set<std::string> allowedDeviceChildren = {
                "Memory", "Debug", "Pcap", "ComParameters", "DataSets", "Interfaces", "MappedDevices"};
            for (auto* child = deviceElem->FirstChildElement(); child; child = child->NextSiblingElement())
            {
                if (!allowedDeviceChildren.count(child->Name()))
                    addIssue(child->GetLineNum(), std::string("Unknown element under <Device>: ") + child->Name());
            }

            if (auto* mem = deviceElem->FirstChildElement("Memory"))
            {
                requireAttr(mem, "memorySize");
            }

            if (auto* comParams = deviceElem->FirstChildElement("ComParameters"))
            {
                for (auto* cp = comParams->FirstChildElement("ComParameter"); cp;
                     cp      = cp->NextSiblingElement("ComParameter"))
                {
                    requireAttr(cp, "id");
                    requireAttr(cp, "qos");
                    requireAttr(cp, "ttl");
                }
            }

            if (auto* dsRoot = deviceElem->FirstChildElement("DataSets"))
            {
                for (auto* ds = dsRoot->FirstChildElement("DataSet"); ds; ds = ds->NextSiblingElement("DataSet"))
                {
                    requireAttr(ds, "id");
                    requireAttr(ds, "name");
                    if (!ds->FirstChildElement("Element"))
                        addIssue(ds->GetLineNum(), "DataSet requires at least one <Element>");
                    for (auto* el = ds->FirstChildElement("Element"); el; el = el->NextSiblingElement("Element"))
                    {
                        requireAttr(el, "name");
                        requireAttr(el, "type");
                    }
                }
            }
            else
            {
                addIssue(deviceElem->GetLineNum(), "Missing <DataSets> definition for TRDP configuration");
            }

            if (auto* ifaces = deviceElem->FirstChildElement("Interfaces"))
            {
                for (auto* iface = ifaces->FirstChildElement("Interface"); iface;
                     iface       = iface->NextSiblingElement("Interface"))
                {
                    requireAttr(iface, "name");
                    requireAttr(iface, "networkId");
                    if (!iface->FirstChildElement("PdCom"))
                        addIssue(iface->GetLineNum(), "Interface missing <PdCom> definition");
                    if (!iface->FirstChildElement("MdCom"))
                        addIssue(iface->GetLineNum(), "Interface missing <MdCom> definition");

                    if (auto* telRoot = iface->FirstChildElement("Telegrams"))
                    {
                        for (auto* tel = telRoot->FirstChildElement("Telegram"); tel;
                             tel      = tel->NextSiblingElement("Telegram"))
                        {
                            requireAttr(tel, "name");
                            requireAttr(tel, "comId");
                            requireAttr(tel, "dataSetId");
                        }
                    }
                    else
                    {
                        addIssue(iface->GetLineNum(), "Interface missing <Telegrams> section");
                    }
                }
            }
            else
            {
                addIssue(deviceElem->GetLineNum(), "Missing <Interfaces> section for TRDP configuration");
            }

            if (auto* mapped = deviceElem->FirstChildElement("MappedDevices"))
            {
                for (auto* dev = mapped->FirstChildElement("MappedDevice"); dev;
                     dev       = dev->NextSiblingElement("MappedDevice"))
                {
                    requireAttr(dev, "hostName");
                    for (auto* iface = dev->FirstChildElement("Interface"); iface;
                         iface       = iface->NextSiblingElement("Interface"))
                    {
                        requireAttr(iface, "name");
                        requireAttr(iface, "hostIp");
                        requireAttr(iface, "leaderIp");
                    }
                }
            }

            return issues;
        }

    } // namespace

    DeviceConfig ConfigManager::loadDeviceConfigFromXml(const std::string& path, bool validateSchema)
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
            throw ConfigError(path, doc.ErrorLineNum(), "Failed to load configuration XML");

        auto* deviceElem = doc.FirstChildElement("Device");
        if (!deviceElem)
            throw ConfigError(path, doc.ErrorLineNum(), "Missing <Device> root element in XML");

        if (validateSchema)
        {
            auto issues = validateSchemaDoc(doc, path);
            if (!issues.empty())
            {
                std::ostringstream oss;
                oss << "Schema validation failed with " << issues.size() << " issue(s):";
                for (const auto& issue : issues)
                {
                    oss << "\n";
                    if (issue.line > 0)
                        oss << "line " << issue.line << ": ";
                    oss << issue.message;
                }
                throw ConfigError(path, issues.front().line, oss.str());
            }
        }

        DeviceConfig cfg;
        cfg.hostName   = parseString(path, deviceElem, "hostName", true);
        cfg.leaderName = parseString(path, deviceElem, "leaderName", false, "");
        cfg.type       = parseString(path, deviceElem, "type", false, "");

        cfg.memory        = parseMemory(path, deviceElem->FirstChildElement("Memory"));
        cfg.debug         = parseDebug(path, deviceElem->FirstChildElement("Debug"));
        cfg.pcap          = parsePcap(path, deviceElem->FirstChildElement("Pcap"));
        cfg.comParameters = parseComParameters(path, deviceElem->FirstChildElement("ComParameters"));

        if (auto* dsRoot = deviceElem->FirstChildElement("DataSets"))
        {
            for (auto* ds = dsRoot->FirstChildElement("DataSet"); ds; ds = ds->NextSiblingElement("DataSet"))
            {
                cfg.dataSets.push_back(parseDataSet(path, ds));
            }
        }

        if (auto* ifaces = deviceElem->FirstChildElement("Interfaces"))
        {
            for (auto* iface = ifaces->FirstChildElement("Interface"); iface;
                 iface       = iface->NextSiblingElement("Interface"))
            {
                cfg.interfaces.push_back(parseInterface(path, iface));
            }
        }

        if (auto* mapped = deviceElem->FirstChildElement("MappedDevices"))
        {
            for (auto* dev = mapped->FirstChildElement("MappedDevice"); dev;
                 dev       = dev->NextSiblingElement("MappedDevice"))
            {
                cfg.mappedDevices.push_back(parseMappedDevice(path, dev));
            }
        }

        return cfg;
    }

    std::vector<SchemaIssue> ConfigManager::validateXmlSchema(const std::string& path)
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
            return {{"Failed to load configuration XML", doc.ErrorLineNum()}};

        return validateSchemaDoc(doc, path);
    }

    void ConfigManager::validateDeviceConfig(const DeviceConfig& cfg)
    {
        std::unordered_set<uint32_t>    dataSetIds;
        std::unordered_set<uint32_t>    comParamIds;
        std::unordered_set<uint32_t>    comIds;
        std::unordered_set<std::string> ifaceNames;

        for (const auto& ds : cfg.dataSets)
        {
            if (!dataSetIds.insert(ds.id).second)
                throw std::runtime_error("Duplicate dataset id: " + std::to_string(ds.id));
        }

        for (const auto& ds : cfg.dataSets)
        {
            if (ds.elements.empty())
                throw std::runtime_error("Dataset has no elements: " + ds.name);
            for (const auto& el : ds.elements)
            {
                auto typeVal = static_cast<int>(el.type);
                if (typeVal <= 0 || typeVal > static_cast<int>(data::ElementType::NESTED_DATASET))
                    throw std::runtime_error("Unsupported dataset element type in " + ds.name + ": " +
                                             std::to_string(el.type));
                if (static_cast<data::ElementType>(el.type) == data::ElementType::NESTED_DATASET)
                {
                    if (!el.nestedDataSetId)
                        throw std::runtime_error("Nested dataset element missing nestedDataSetId in " + ds.name);
                    if (dataSetIds.find(*el.nestedDataSetId) == dataSetIds.end())
                        throw std::runtime_error("Nested dataset element references unknown dataset id " +
                                                 std::to_string(*el.nestedDataSetId));
                }
            }
        }

        for (const auto& cp : cfg.comParameters)
        {
            if (!comParamIds.insert(cp.id).second)
                throw std::runtime_error("Duplicate comParameter id: " + std::to_string(cp.id));
        }

        if (cfg.pcap && cfg.pcap->enabled)
        {
            if (cfg.pcap->fileName.empty())
                throw std::runtime_error("PCAP capture enabled but fileName is missing");
            if (!cfg.pcap->captureTx && !cfg.pcap->captureRx)
                throw std::runtime_error("PCAP capture must enable at least one of captureTx or captureRx");
        }

        for (const auto& iface : cfg.interfaces)
        {
            if (!ifaceNames.insert(iface.name).second)
                throw std::runtime_error("Duplicate interface name: " + iface.name);
            if (iface.pdCom.port == 0)
                throw std::runtime_error("Invalid PD port on interface " + iface.name);
            if (iface.mdCom.udpPort == 0 || iface.mdCom.tcpPort == 0)
                throw std::runtime_error("Invalid MD port on interface " + iface.name);
            if (iface.mdCom.replyTimeoutUs == 0)
                throw std::runtime_error("MD replyTimeoutUs must be positive on interface " + iface.name);
            if (iface.mdCom.confirmTimeoutUs == 0)
                throw std::runtime_error("MD confirmTimeoutUs must be positive on interface " + iface.name);
            if (iface.mdCom.protocol == MdComParameter::Protocol::TCP && iface.mdCom.connectTimeoutUs == 0)
                throw std::runtime_error("MD connectTimeoutUs must be set for TCP on interface " + iface.name);
            if (iface.mdCom.retries > 10)
                throw std::runtime_error("MD retries out of supported range (0-10) on interface " + iface.name);

            std::unordered_set<std::string> multicastAddrs;
            for (const auto& group : iface.multicastGroups)
            {
                if (group.address.empty())
                    throw std::runtime_error("Multicast group missing address on interface " + iface.name);
                if (!multicastAddrs.insert(group.address).second)
                    throw std::runtime_error("Duplicate multicast group address on interface " + iface.name + ": " +
                                             group.address);
            }

            for (const auto& tel : iface.telegrams)
            {
                if (!comIds.insert(tel.comId).second)
                    throw std::runtime_error("Duplicate COM ID: " + std::to_string(tel.comId));
                if (dataSetIds.find(tel.dataSetId) == dataSetIds.end())
                    throw std::runtime_error("Telegram references unknown dataset id " + std::to_string(tel.dataSetId));
                if (tel.comParameterId != 0 && comParamIds.find(tel.comParameterId) == comParamIds.end())
                    throw std::runtime_error("Telegram references unknown comParameterId " +
                                             std::to_string(tel.comParameterId));

                if (tel.pdParam)
                {
                    if (tel.pdParam->cycleUs == 0 || tel.pdParam->cycleUs > 60 * 1000 * 1000)
                        throw std::runtime_error("PD cycle time out of range for COM ID " + std::to_string(tel.comId));
                    if (tel.pdParam->timeoutUs < tel.pdParam->cycleUs)
                        throw std::runtime_error("PD timeout shorter than cycle for COM ID " +
                                                 std::to_string(tel.comId));
                    if (tel.pdParam->redundant > 0)
                    {
                        if (tel.destinations.size() < 2)
                            throw std::runtime_error(
                                "Redundant PD telegram requires at least two destinations (COM ID " +
                                std::to_string(tel.comId) + ")");
                        if (tel.pdParam->redundant >= tel.destinations.size())
                            throw std::runtime_error("Redundant channel index exceeds destination count (COM ID " +
                                                     std::to_string(tel.comId) + ")");
                    }
                }

                std::unordered_set<uint32_t> dstIds;
                for (const auto& dst : tel.destinations)
                {
                    if (!dstIds.insert(dst.id).second)
                        throw std::runtime_error("Duplicate destination id in COM ID " + std::to_string(tel.comId));
                    if (dst.uri.empty())
                        throw std::runtime_error("Destination missing URI for COM ID " + std::to_string(tel.comId));
                }
            }
        }

        for (const auto& mappedDev : cfg.mappedDevices)
        {
            for (const auto& iface : mappedDev.interfaces)
            {
                if (iface.hostIp.empty() || iface.leaderIp.empty())
                    throw std::runtime_error("Mapped interface missing host/leader IP: " + iface.name);
            }
        }
    }

    std::vector<data::DataSetDef> ConfigManager::buildDataSetDefs(const DeviceConfig& cfg)
    {
        std::vector<data::DataSetDef> defs;
        for (const auto& dsCfg : cfg.dataSets)
        {
            data::DataSetDef def;
            def.id   = dsCfg.id;
            def.name = dsCfg.name;
            for (const auto& elemCfg : dsCfg.elements)
            {
                data::ElementDef ed;
                ed.name      = elemCfg.name;
                ed.type      = static_cast<data::ElementType>(elemCfg.type);
                ed.arraySize = elemCfg.arraySize;
                ed.nestedDataSetId = elemCfg.nestedDataSetId;
                def.elements.push_back(std::move(ed));
            }
            defs.push_back(std::move(def));
        }
        return defs;
    }

} // namespace config
