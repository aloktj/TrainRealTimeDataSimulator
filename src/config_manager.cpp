#include "config_manager.hpp"

// TODO: include XML parsing library headers (tinyxml2/libxml2)

namespace config {

DeviceConfig ConfigManager::loadDeviceConfigFromXml(const std::string& path)
{
    DeviceConfig cfg;

    // TODO: parse XML file at 'path'
    // - Fill cfg.memory, cfg.debug, cfg.interfaces, cfg.dataSets, etc.

    return cfg;
}

void ConfigManager::validateDeviceConfig(const DeviceConfig& cfg)
{
    // TODO: validate ranges, unique IDs, ports, etc.
    (void)cfg;
}

std::vector<data::DataSetDef> ConfigManager::buildDataSetDefs(const DeviceConfig& cfg)
{
    std::vector<data::DataSetDef> defs;
    // TODO: convert config::DataSetConfig -> data::DataSetDef
    (void)cfg;
    return defs;
}

} // namespace config
