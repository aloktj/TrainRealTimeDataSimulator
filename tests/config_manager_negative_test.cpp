#include <gtest/gtest.h>

#include "config_manager.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{

    std::filesystem::path writeTempConfig(const std::string& name, const std::string& xml)
    {
        auto          path = std::filesystem::temp_directory_path() / name;
        std::ofstream ofs(path);
        ofs << xml;
        ofs.close();
        return path;
    }

} // namespace

TEST(ConfigManagerNegative, RejectsMissingDeviceRoot)
{
    auto                  path = writeTempConfig("missing_root.xml", "<NotDevice></NotDevice>");
    config::ConfigManager mgr;
    EXPECT_THROW(mgr.loadDeviceConfigFromXml(path.string()), std::runtime_error);
}

TEST(ConfigManagerNegative, RejectsUnknownElementTypes)
{
    const std::string xml =
        "<Device hostName=\"bad\">"
        "<DataSets><DataSet name=\"ds\" id=\"1\"><Element name=\"e1\" type=\"UNKNOWN\"/></DataSet></DataSets>"
        "<Interfaces><Interface networkId=\"1\" name=\"if1\">"
        "<PdCom port=\"17224\" qos=\"1\" ttl=\"1\" timeoutUs=\"1000\"/>"
        "<MdCom udpPort=\"17225\" tcpPort=\"17226\" replyTimeoutUs=\"1\" confirmTimeoutUs=\"1\"/>"
        "</Interface></Interfaces>"
        "</Device>";

    auto                  path = writeTempConfig("unknown_type.xml", xml);
    config::ConfigManager mgr;
    EXPECT_THROW(mgr.loadDeviceConfigFromXml(path.string()), std::runtime_error);
}

TEST(ConfigManagerNegative, ValidatesEmptyDatasetsAndInterfaces)
{
    const std::string xml = "<Device hostName=\"invalid\">"
                            "<DataSets><DataSet name=\"empty\" id=\"1\"></DataSet></DataSets>"
                            "<Interfaces><Interface networkId=\"1\" name=\"if1\">"
                            "<PdCom port=\"0\" qos=\"1\" ttl=\"1\" timeoutUs=\"1000\"/>"
                            "<MdCom udpPort=\"0\" tcpPort=\"0\" replyTimeoutUs=\"0\" confirmTimeoutUs=\"0\"/>"
                            "</Interface></Interfaces>"
                            "</Device>";

    auto                  path = writeTempConfig("invalid_values.xml", xml);
    config::ConfigManager mgr;
    auto                  cfg = mgr.loadDeviceConfigFromXml(path.string());
    EXPECT_THROW(mgr.validateDeviceConfig(cfg), std::runtime_error);
}
