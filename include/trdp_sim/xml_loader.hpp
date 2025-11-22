#pragma once

#include <string>
#include <vector>

#include "config_manager.hpp"

namespace config
{
    /**
     * Lightweight XML loader that performs schema validation for the
     * TCNOpen/TRDP configuration model before handing parsing over to
     * ConfigManager. Issues are reported with line numbers to aid UI and
     * operator feedback.
     */
    class XmlConfigurationLoader
    {
      public:
        config::DeviceConfig load(const std::string& path) const;

        std::vector<SchemaIssue> validateOnly(const std::string& path) const;

      private:
        DeviceConfig parseWithManager(const std::string& path) const;
    };

} // namespace config

