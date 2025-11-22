#include "config_manager.hpp"

#include "data_types.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <tinyxml2.h>

namespace config {

namespace {

using tinyxml2::XMLElement;

template <typename T>
T parseUnsigned(XMLElement* elem, const char* attr, bool required, T defaultValue = {})
{
    const char* txt = elem->Attribute(attr);
    if (!txt) {
        if (required)
            throw std::runtime_error(std::string("Missing required attribute '") + attr + "'");
        return defaultValue;
    }
    unsigned long long v = std::stoull(txt);
    return static_cast<T>(v);
}

bool parseBool(XMLElement* elem, const char* attr, bool defaultValue = false)
{
    const char* txt = elem->Attribute(attr);
    if (!txt)
        return defaultValue;
    std::string_view sv(txt);
    return sv == "1" || sv == "true" || sv == "TRUE" || sv == "True";
}

std::string parseString(XMLElement* elem, const char* attr, bool required, const std::string& defaultValue = {})
{
    const char* txt = elem->Attribute(attr);
    if (!txt) {
        if (required)
            throw std::runtime_error(std::string("Missing required attribute '") + attr + "'");
        return defaultValue;
    }
    return txt;
}

data::ElementType parseElementType(const std::string& name)
{
    static const std::unordered_map<std::string, data::ElementType> map = {
        {"BOOL8", data::ElementType::BOOL8},       {"CHAR8", data::ElementType::CHAR8},
        {"UTF16", data::ElementType::UTF16},       {"INT8", data::ElementType::INT8},
        {"INT16", data::ElementType::INT16},       {"INT32", data::ElementType::INT32},
        {"INT64", data::ElementType::INT64},       {"UINT8", data::ElementType::UINT8},
        {"UINT16", data::ElementType::UINT16},     {"UINT32", data::ElementType::UINT32},
        {"UINT64", data::ElementType::UINT64},     {"REAL32", data::ElementType::REAL32},
        {"REAL64", data::ElementType::REAL64},     {"TIMEDATE32", data::ElementType::TIMEDATE32},
        {"TIMEDATE48", data::ElementType::TIMEDATE48}, {"TIMEDATE64", data::ElementType::TIMEDATE64},
        {"NESTED_DATASET", data::ElementType::NESTED_DATASET},
    };

    auto it = map.find(name);
    if (it == map.end())
        throw std::runtime_error("Unsupported element type: " + name);
    return it->second;
}

PdComParameter::ValidityBehavior parseValidityBehavior(const std::string& name)
{
    if (name == "ZERO")
        return PdComParameter::ValidityBehavior::ZERO;
    if (name == "KEEP")
        return PdComParameter::ValidityBehavior::KEEP;
    throw std::runtime_error("Unknown validityBehavior: " + name);
}

MdComParameter::Protocol parseMdProtocol(const std::string& name)
{
    if (name == "TCP")
        return MdComParameter::Protocol::TCP;
    return MdComParameter::Protocol::UDP;
}

MemoryConfig parseMemory(XMLElement* memElem)
{
    MemoryConfig mem;
    if (!memElem)
        return mem;

    mem.memorySize = parseUnsigned<uint32_t>(memElem, "memorySize", false, 0);
    for (auto* blk = memElem->FirstChildElement("Block"); blk; blk = blk->NextSiblingElement("Block")) {
        MemBlockConfig b;
        b.size = parseUnsigned<uint32_t>(blk, "size", true);
        b.preallocate = parseUnsigned<uint32_t>(blk, "preallocate", false, 0);
        mem.blocks.push_back(b);
    }
    return mem;
}

std::optional<DebugConfig> parseDebug(XMLElement* dbgElem)
{
    if (!dbgElem)
        return std::nullopt;
    DebugConfig d;
    d.fileName = parseString(dbgElem, "fileName", true);
    d.fileSize = parseUnsigned<uint32_t>(dbgElem, "fileSize", false, 0);
    d.info = parseString(dbgElem, "info", false, {});
    auto levelStr = parseString(dbgElem, "level", false, "W");
    d.level = levelStr.empty() ? 'W' : levelStr[0];
    return d;
}

std::vector<ComParameter> parseComParameters(XMLElement* parent)
{
    std::vector<ComParameter> out;
    if (!parent)
        return out;
    for (auto* c = parent->FirstChildElement("ComParameter"); c; c = c->NextSiblingElement("ComParameter")) {
        ComParameter p;
        p.id = parseUnsigned<uint32_t>(c, "id", true);
        p.qos = parseUnsigned<uint8_t>(c, "qos", true);
        p.ttl = parseUnsigned<uint8_t>(c, "ttl", true);
        out.push_back(p);
    }
    return out;
}

SdtParameter parseSdt(XMLElement* elem)
{
    SdtParameter s;
    if (!elem)
        return s;
    s.smi1 = parseUnsigned<uint16_t>(elem, "smi1", true);
    s.smi2 = parseUnsigned<uint16_t>(elem, "smi2", true);
    s.udv = parseUnsigned<uint8_t>(elem, "udv", true);
    s.rxPeriodMs = parseUnsigned<uint32_t>(elem, "rxPeriodMs", true);
    s.txPeriodMs = parseUnsigned<uint32_t>(elem, "txPeriodMs", true);
    s.nRxsafe = parseUnsigned<uint32_t>(elem, "nRxsafe", true);
    s.nGrard = parseUnsigned<uint32_t>(elem, "nGrard", true);
    s.cmThr = parseUnsigned<uint32_t>(elem, "cmThr", true);
    return s;
}

DestinationConfig parseDestination(XMLElement* elem)
{
    DestinationConfig d;
    d.id = parseUnsigned<uint32_t>(elem, "id", true);
    d.uri = parseString(elem, "uri", true);
    d.name = parseString(elem, "name", false, {});
    if (auto* sdtElem = elem->FirstChildElement("Sdt"))
        d.sdt = parseSdt(sdtElem);
    return d;
}

PdParameter parsePdParameters(XMLElement* elem)
{
    PdParameter p;
    p.cycleUs = parseUnsigned<uint32_t>(elem, "cycleUs", true);
    p.marshall = parseBool(elem, "marshall", true);
    p.timeoutUs = parseUnsigned<uint32_t>(elem, "timeoutUs", true);
    p.validityBehavior = parseValidityBehavior(parseString(elem, "validityBehavior", false, "KEEP"));
    p.redundant = parseUnsigned<uint8_t>(elem, "redundant", false, 0);
    p.callback = parseBool(elem, "callback", false);
    p.offsetAddress = parseUnsigned<uint32_t>(elem, "offsetAddress", false, 0);
    return p;
}

DataElementConfig parseElement(XMLElement* elem)
{
    DataElementConfig cfg;
    cfg.name = parseString(elem, "name", true);
    auto typeStr = parseString(elem, "type", true);
    cfg.type = static_cast<uint32_t>(parseElementType(typeStr));
    cfg.arraySize = parseUnsigned<uint32_t>(elem, "arraySize", false, 1);
    return cfg;
}

DataSetConfig parseDataSet(XMLElement* elem)
{
    DataSetConfig ds;
    ds.name = parseString(elem, "name", true);
    ds.id = parseUnsigned<uint32_t>(elem, "id", true);
    for (auto* el = elem->FirstChildElement("Element"); el; el = el->NextSiblingElement("Element")) {
        ds.elements.push_back(parseElement(el));
    }
    return ds;
}

TrdpProcessConfig parseTrdpProcess(XMLElement* elem)
{
    TrdpProcessConfig cfg;
    if (!elem)
        return cfg;
    cfg.blocking = parseBool(elem, "blocking", false);
    cfg.cycleTimeUs = parseUnsigned<uint32_t>(elem, "cycleTimeUs", false, 0);
    cfg.priority = parseUnsigned<uint8_t>(elem, "priority", false, 0);
    cfg.trafficShaping = parseBool(elem, "trafficShaping", false);
    return cfg;
}

PdComParameter parsePdCom(XMLElement* elem)
{
    if (!elem)
        throw std::runtime_error("Interface missing <PdCom> definition");
    PdComParameter cfg;
    cfg.marshall = parseBool(elem, "marshall", true);
    cfg.port = parseUnsigned<uint16_t>(elem, "port", true);
    cfg.qos = parseUnsigned<uint8_t>(elem, "qos", true);
    cfg.ttl = parseUnsigned<uint8_t>(elem, "ttl", true);
    cfg.timeoutUs = parseUnsigned<uint32_t>(elem, "timeoutUs", true);
    cfg.validityBehavior = parseValidityBehavior(parseString(elem, "validityBehavior", false, "KEEP"));
    cfg.callbackEnabled = parseBool(elem, "callbackEnabled", false);
    return cfg;
}

MdComParameter parseMdCom(XMLElement* elem)
{
    if (!elem)
        throw std::runtime_error("Interface missing <MdCom> definition");
    MdComParameter cfg;
    cfg.udpPort = parseUnsigned<uint16_t>(elem, "udpPort", true);
    cfg.tcpPort = parseUnsigned<uint16_t>(elem, "tcpPort", true);
    cfg.confirmTimeoutUs = parseUnsigned<uint32_t>(elem, "confirmTimeoutUs", false, 0);
    cfg.connectTimeoutUs = parseUnsigned<uint32_t>(elem, "connectTimeoutUs", false, 0);
    cfg.replyTimeoutUs = parseUnsigned<uint32_t>(elem, "replyTimeoutUs", false, 0);
    cfg.marshall = parseBool(elem, "marshall", true);
    cfg.protocol = parseMdProtocol(parseString(elem, "protocol", false, "UDP"));
    cfg.qos = parseUnsigned<uint8_t>(elem, "qos", false, 0);
    cfg.ttl = parseUnsigned<uint8_t>(elem, "ttl", false, 0);
    cfg.retries = parseUnsigned<uint8_t>(elem, "retries", false, 0);
    cfg.numSessions = parseUnsigned<uint32_t>(elem, "numSessions", false, 0);
    return cfg;
}

TelegramConfig parseTelegram(XMLElement* elem)
{
    TelegramConfig t;
    t.name = parseString(elem, "name", true);
    t.comId = parseUnsigned<uint32_t>(elem, "comId", true);
    t.dataSetId = parseUnsigned<uint32_t>(elem, "dataSetId", true);
    t.comParameterId = parseUnsigned<uint32_t>(elem, "comParameterId", false, 0);
    if (auto* pd = elem->FirstChildElement("PdParameters"))
        t.pdParam = parsePdParameters(pd);

    if (auto* dests = elem->FirstChildElement("Destinations")) {
        for (auto* d = dests->FirstChildElement("Destination"); d; d = d->NextSiblingElement("Destination")) {
            t.destinations.push_back(parseDestination(d));
        }
    }

    return t;
}

BusInterfaceConfig parseInterface(XMLElement* elem)
{
    BusInterfaceConfig iface;
    iface.networkId = parseUnsigned<uint32_t>(elem, "networkId", true);
    iface.name = parseString(elem, "name", true);
    if (const char* hostIp = elem->Attribute("hostIp"))
        iface.hostIp = hostIp;

    iface.trdpProcess = parseTrdpProcess(elem->FirstChildElement("TrdpProcess"));
    iface.pdCom = parsePdCom(elem->FirstChildElement("PdCom"));
    iface.mdCom = parseMdCom(elem->FirstChildElement("MdCom"));

    if (auto* tRoot = elem->FirstChildElement("Telegrams")) {
        for (auto* t = tRoot->FirstChildElement("Telegram"); t; t = t->NextSiblingElement("Telegram")) {
            iface.telegrams.push_back(parseTelegram(t));
        }
    }

    return iface;
}

MappedTelegramConfig parseMappedTelegram(XMLElement* elem)
{
    MappedTelegramConfig t;
    t.comId = parseUnsigned<uint32_t>(elem, "comId", true);
    t.name = parseString(elem, "name", true);
    return t;
}

MappedBusInterfaceConfig parseMappedInterface(XMLElement* elem)
{
    MappedBusInterfaceConfig iface;
    iface.name = parseString(elem, "name", true);
    iface.hostIp = parseString(elem, "hostIp", true);
    iface.leaderIp = parseString(elem, "leaderIp", true);
    for (auto* t = elem->FirstChildElement("MappedTelegram"); t; t = t->NextSiblingElement("MappedTelegram")) {
        iface.mappedTelegrams.push_back(parseMappedTelegram(t));
    }
    return iface;
}

MappedDeviceConfig parseMappedDevice(XMLElement* elem)
{
    MappedDeviceConfig dev;
    dev.hostName = parseString(elem, "hostName", true);
    dev.leaderName = parseString(elem, "leaderName", false, "");
    for (auto* iface = elem->FirstChildElement("Interface"); iface; iface = iface->NextSiblingElement("Interface")) {
        dev.interfaces.push_back(parseMappedInterface(iface));
    }
    return dev;
}

} // namespace

DeviceConfig ConfigManager::loadDeviceConfigFromXml(const std::string& path)
{
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("Failed to load configuration XML: " + path);

    auto* deviceElem = doc.FirstChildElement("Device");
    if (!deviceElem)
        throw std::runtime_error("Missing <Device> root element in XML");

    DeviceConfig cfg;
    cfg.hostName = parseString(deviceElem, "hostName", true);
    cfg.leaderName = parseString(deviceElem, "leaderName", false, "");
    cfg.type = parseString(deviceElem, "type", false, "");

    cfg.memory = parseMemory(deviceElem->FirstChildElement("Memory"));
    cfg.debug = parseDebug(deviceElem->FirstChildElement("Debug"));
    cfg.comParameters = parseComParameters(deviceElem->FirstChildElement("ComParameters"));

    if (auto* dsRoot = deviceElem->FirstChildElement("DataSets")) {
        for (auto* ds = dsRoot->FirstChildElement("DataSet"); ds; ds = ds->NextSiblingElement("DataSet")) {
            cfg.dataSets.push_back(parseDataSet(ds));
        }
    }

    if (auto* ifaces = deviceElem->FirstChildElement("Interfaces")) {
        for (auto* iface = ifaces->FirstChildElement("Interface"); iface; iface = iface->NextSiblingElement("Interface")) {
            cfg.interfaces.push_back(parseInterface(iface));
        }
    }

    if (auto* mapped = deviceElem->FirstChildElement("MappedDevices")) {
        for (auto* dev = mapped->FirstChildElement("MappedDevice"); dev; dev = dev->NextSiblingElement("MappedDevice")) {
            cfg.mappedDevices.push_back(parseMappedDevice(dev));
        }
    }

    return cfg;
}

void ConfigManager::validateDeviceConfig(const DeviceConfig& cfg)
{
    std::unordered_set<uint32_t> dataSetIds;
    std::unordered_set<uint32_t> comParamIds;
    std::unordered_set<uint32_t> comIds;
    std::unordered_set<std::string> ifaceNames;

    for (const auto& ds : cfg.dataSets) {
        if (!dataSetIds.insert(ds.id).second)
            throw std::runtime_error("Duplicate dataset id: " + std::to_string(ds.id));
        if (ds.elements.empty())
            throw std::runtime_error("Dataset has no elements: " + ds.name);
        for (const auto& el : ds.elements) {
            auto typeVal = static_cast<int>(el.type);
            if (typeVal <= 0 || typeVal > static_cast<int>(data::ElementType::NESTED_DATASET))
                throw std::runtime_error("Unsupported dataset element type in " + ds.name + ": " + std::to_string(el.type));
        }
    }

    for (const auto& cp : cfg.comParameters) {
        if (!comParamIds.insert(cp.id).second)
            throw std::runtime_error("Duplicate comParameter id: " + std::to_string(cp.id));
    }

    for (const auto& iface : cfg.interfaces) {
        if (!ifaceNames.insert(iface.name).second)
            throw std::runtime_error("Duplicate interface name: " + iface.name);
        if (iface.pdCom.port == 0)
            throw std::runtime_error("Invalid PD port on interface " + iface.name);
        if (iface.mdCom.udpPort == 0 || iface.mdCom.tcpPort == 0)
            throw std::runtime_error("Invalid MD port on interface " + iface.name);

        for (const auto& tel : iface.telegrams) {
            if (!comIds.insert(tel.comId).second)
                throw std::runtime_error("Duplicate COM ID: " + std::to_string(tel.comId));
            if (dataSetIds.find(tel.dataSetId) == dataSetIds.end())
                throw std::runtime_error("Telegram references unknown dataset id " + std::to_string(tel.dataSetId));
            if (tel.comParameterId != 0 && comParamIds.find(tel.comParameterId) == comParamIds.end())
                throw std::runtime_error("Telegram references unknown comParameterId " + std::to_string(tel.comParameterId));

            if (tel.pdParam) {
                if (tel.pdParam->cycleUs == 0 || tel.pdParam->cycleUs > 60 * 1000 * 1000)
                    throw std::runtime_error("PD cycle time out of range for COM ID " + std::to_string(tel.comId));
                if (tel.pdParam->timeoutUs < tel.pdParam->cycleUs)
                    throw std::runtime_error("PD timeout shorter than cycle for COM ID " + std::to_string(tel.comId));
                if (tel.pdParam->redundant > 0) {
                    if (tel.destinations.size() < 2)
                        throw std::runtime_error("Redundant PD telegram requires at least two destinations (COM ID " + std::to_string(tel.comId) + ")");
                    if (tel.pdParam->redundant >= tel.destinations.size())
                        throw std::runtime_error("Redundant channel index exceeds destination count (COM ID " + std::to_string(tel.comId) + ")");
                }
            }

            std::unordered_set<uint32_t> dstIds;
            for (const auto& dst : tel.destinations) {
                if (!dstIds.insert(dst.id).second)
                    throw std::runtime_error("Duplicate destination id in COM ID " + std::to_string(tel.comId));
                if (dst.uri.empty())
                    throw std::runtime_error("Destination missing URI for COM ID " + std::to_string(tel.comId));
            }
        }
    }

    for (const auto& mappedDev : cfg.mappedDevices) {
        for (const auto& iface : mappedDev.interfaces) {
            if (iface.hostIp.empty() || iface.leaderIp.empty())
                throw std::runtime_error("Mapped interface missing host/leader IP: " + iface.name);
        }
    }
}

std::vector<data::DataSetDef> ConfigManager::buildDataSetDefs(const DeviceConfig& cfg)
{
    std::vector<data::DataSetDef> defs;
    for (const auto& dsCfg : cfg.dataSets) {
        data::DataSetDef def;
        def.id = dsCfg.id;
        def.name = dsCfg.name;
        for (const auto& elemCfg : dsCfg.elements) {
            data::ElementDef ed;
            ed.name = elemCfg.name;
            ed.type = static_cast<data::ElementType>(elemCfg.type);
            ed.arraySize = elemCfg.arraySize;
            def.elements.push_back(std::move(ed));
        }
        defs.push_back(std::move(def));
    }
    return defs;
}

} // namespace config
