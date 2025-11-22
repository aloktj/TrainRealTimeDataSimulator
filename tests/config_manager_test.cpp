#include <gtest/gtest.h>

#include "config_manager.hpp"
#include "data_types.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

TEST(ConfigManager, ParsesSampleConfig)
{
    const auto configPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "sample_ci_device.xml";
    config::ConfigManager mgr;
    auto                  cfg = mgr.loadDeviceConfigFromXml(configPath.string());
    mgr.validateDeviceConfig(cfg);

    EXPECT_EQ(cfg.hostName, "ci-device");
    ASSERT_EQ(cfg.dataSets.size(), 3u);
    EXPECT_EQ(cfg.interfaces.size(), 1u);

    auto defs = mgr.buildDataSetDefs(cfg);
    ASSERT_EQ(defs.size(), 3u);
    EXPECT_EQ(defs[0].elements.size(), 2u);
    EXPECT_EQ(defs[1].elements.size(), 2u);
    EXPECT_EQ(defs[2].elements.size(), 2u);
}

TEST(ConfigManager, DetectsDuplicateDatasetIds)
{
    const std::string tmpPath = std::filesystem::temp_directory_path() / "dup_dataset.xml";
    std::ofstream     ofs(tmpPath);
    ofs << "<Device hostName=\"dup\">"
           "<DataSets>"
           "<DataSet name=\"ds1\" id=\"1\"><Element name=\"e1\" type=\"UINT8\"/></DataSet>"
           "<DataSet name=\"ds2\" id=\"1\"><Element name=\"e2\" type=\"UINT8\"/></DataSet>"
           "</DataSets>"
           "<Interfaces><Interface networkId=\"1\" name=\"if1\">"
           "<PdCom port=\"17224\" qos=\"1\" ttl=\"1\" timeoutUs=\"1000\"/>"
           "<MdCom udpPort=\"17225\" tcpPort=\"17226\"/></Interface></Interfaces>"
           "</Device>";
    ofs.close();

    config::ConfigManager mgr;
    auto                  cfg = mgr.loadDeviceConfigFromXml(tmpPath);
    EXPECT_THROW(mgr.validateDeviceConfig(cfg), std::runtime_error);
}

TEST(ConfigManager, DetectsInvalidMdTimeouts)
{
    const std::string tmpPath = std::filesystem::temp_directory_path() / "bad_mdcom.xml";
    std::ofstream     ofs(tmpPath);
    ofs << "<Device hostName=\"md\">"
           "<DataSets><DataSet name=\"ds\" id=\"1\"><Element name=\"e1\" type=\"UINT8\"/></DataSet></DataSets>"
           "<Interfaces><Interface networkId=\"1\" name=\"if1\">"
           "<PdCom port=\"17224\" qos=\"1\" ttl=\"1\" timeoutUs=\"1000\"/>"
           "<MdCom udpPort=\"17225\" tcpPort=\"17226\" replyTimeoutUs=\"0\" confirmTimeoutUs=\"0\" retries=\"11\" "
           "protocol=\"TCP\" connectTimeoutUs=\"0\"/>"
           "</Interface></Interfaces>"
           "</Device>";
    ofs.close();

    config::ConfigManager mgr;
    auto                  cfg = mgr.loadDeviceConfigFromXml(tmpPath);
    try
    {
        mgr.validateDeviceConfig(cfg);
        FAIL() << "Expected validation failure";
    }
    catch (const std::runtime_error& ex)
    {
        const std::string msg = ex.what();
        EXPECT_NE(msg.find("replyTimeoutUs"), std::string::npos);
    }
}
