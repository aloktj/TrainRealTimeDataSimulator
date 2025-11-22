#include "xml_loader.hpp"

#include <sstream>

namespace config
{

    DeviceConfig XmlConfigurationLoader::parseWithManager(const std::string& path) const
    {
        ConfigManager mgr;
        auto          cfg = mgr.loadDeviceConfigFromXml(path, true);
        mgr.validateDeviceConfig(cfg);
        return cfg;
    }

    DeviceConfig XmlConfigurationLoader::load(const std::string& path) const
    {
        return parseWithManager(path);
    }

    std::vector<SchemaIssue> XmlConfigurationLoader::validateOnly(const std::string& path) const
    {
        ConfigManager mgr;
        return mgr.validateXmlSchema(path);
    }

} // namespace config

