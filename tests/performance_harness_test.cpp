#include <gtest/gtest.h>

#include "config_manager.hpp"
#include "data_marshalling.hpp"
#include "performance_harness.hpp"
#include "trdp_adapter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{

    trdp_sim::EngineContext buildContextFromConfig()
    {
        config::ConfigManager   mgr;
        trdp_sim::EngineContext ctx;
        const auto              configPath =
            std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "sample_ci_device.xml";
        ctx.deviceConfig = mgr.loadDeviceConfigFromXml(configPath.string());
        mgr.validateDeviceConfig(ctx.deviceConfig);

        auto defs = mgr.buildDataSetDefs(ctx.deviceConfig);
        for (auto& def : defs)
        {
            ctx.dataSetDefs[def.id] = def;
            auto inst               = std::make_unique<data::DataSetInstance>();
            inst->def               = &ctx.dataSetDefs[def.id];
            inst->values.resize(def.elements.size());
            ctx.dataSetInstances[def.id] = std::move(inst);
        }
        return ctx;
    }

} // namespace

TEST(PerformanceHarnessTest, MeetsPdMdAndWebUiThresholds)
{
    using namespace std::chrono_literals;
    trdp_sim::perf::BenchmarkHarness harness;

    auto start = trdp_sim::perf::BenchmarkHarness::Clock::now();
    for (std::size_t i = 0; i < 500; ++i)
        harness.recordPdTelegram(start + std::chrono::microseconds(1000 * i));

    for (int i = 0; i < 200; ++i)
        harness.mdSessionStarted();
    for (int i = 0; i < 200; ++i)
        harness.mdSessionFinished();

    for (int i = 0; i < 20; ++i)
        harness.recordWebUiUpdate(start + 10ms * i);

    auto report = harness.snapshot();
    EXPECT_EQ(report.pdTelegramCount, 500u);
    EXPECT_LE(report.pdJitterMicros, 1000.0);
    EXPECT_GE(report.mdPeakConcurrency, 200u);
    EXPECT_GE(report.webUiUpdateRateHz, 10.0);
    EXPECT_TRUE(harness.meetsThresholds(trdp_sim::perf::Platform::VM, {}));
    EXPECT_TRUE(harness.meetsThresholds(trdp_sim::perf::Platform::RaspberryPi, {}));
    EXPECT_FALSE(report.toJson().empty());
}

TEST(ResilienceTest, RecoversMulticastAfterInterfaceReset)
{
    auto ctx = buildContextFromConfig();
    ASSERT_FALSE(ctx.deviceConfig.interfaces.empty());

    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    adapter.init();

    const auto& iface = ctx.deviceConfig.interfaces.front();
    adapter.applyMulticastConfig(iface);
    auto state = adapter.getMulticastState();
    ASSERT_FALSE(state.empty());

    for (const auto& entry : state)
        adapter.leaveMulticast(entry.ifaceName, entry.address);

    auto cleared = adapter.getMulticastState();
    for (const auto& entry : cleared)
        EXPECT_FALSE(entry.joined);

    EXPECT_TRUE(adapter.recoverInterface(iface));

    auto recovered = adapter.getMulticastState();
    EXPECT_EQ(recovered.size(), iface.multicastGroups.size());
    for (const auto& entry : recovered)
        EXPECT_TRUE(entry.joined);
}

TEST(ResilienceTest, HandlesMalformedXmlGracefully)
{
    const auto tmp = std::filesystem::temp_directory_path() / "bad_trdp.xml";
    std::ofstream  os(tmp);
    os << "<Device><Bad></Bad></Device>";
    os.close();

    config::ConfigManager mgr;
    EXPECT_THROW(mgr.loadDeviceConfigFromXml(tmp.string()), config::ConfigError);
}

TEST(ResilienceTest, MalformedDatasetDecodingDoesNotCrash)
{
    auto ctx = buildContextFromConfig();
    auto it  = ctx.dataSetInstances.find(3);
    ASSERT_NE(it, ctx.dataSetInstances.end());
    auto& inst = *it->second;

    trdp_sim::util::unmarshalDataToDataSet(inst, ctx, nullptr, 0);
    for (const auto& cell : inst.values)
    {
        EXPECT_FALSE(cell.defined);
        EXPECT_FALSE(cell.raw.empty());
    }
}

