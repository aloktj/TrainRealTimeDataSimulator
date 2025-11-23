#include <gtest/gtest.h>

#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include "config_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{

    std::string buildTwoPdConfig()
    {
        const std::string path = std::filesystem::temp_directory_path() / "pd_sched.xml";
        std::ofstream     ofs(path);
        ofs << "<Device hostName=\"sched\">"
               "<DataSets><DataSet name=\"ds\" id=\"1\"><Element name=\"raw\" type=\"UINT8\"/></DataSet></DataSets>"
               "<Interfaces><Interface networkId=\"1\" name=\"if1\">"
               "<PdCom port=\"17224\" qos=\"1\" ttl=\"1\" timeoutUs=\"5000\" validityBehavior=\"ZERO\"/>"
               "<MdCom udpPort=\"17225\" tcpPort=\"17226\"/>"
               "<Telegrams>"
               "<Telegram name=\"Fast\" comId=\"100\" dataSetId=\"1\" comParameterId=\"1\">"
               "<PdParameters cycleUs=\"2000\" marshall=\"false\" timeoutUs=\"8000\" validityBehavior=\"KEEP\" redundant=\"1\"/>"
               "<Destinations><Destination id=\"1\" uri=\"239.0.0.1\"/><Destination id=\"2\" uri=\"239.0.0.2\"/></Destinations>"
               "</Telegram>"
               "<Telegram name=\"Slow\" comId=\"101\" dataSetId=\"1\" comParameterId=\"1\">"
               "<PdParameters cycleUs=\"4000\" marshall=\"false\" timeoutUs=\"8000\" validityBehavior=\"KEEP\" redundant=\"1\"/>"
               "<Destinations><Destination id=\"1\" uri=\"239.0.0.3\"/><Destination id=\"2\" uri=\"239.0.0.4\"/></Destinations>"
               "</Telegram>"
               "</Telegrams>"
               "</Interface></Interfaces>"
               "</Device>";
        return path;
    }

    std::unique_ptr<trdp_sim::EngineContext> buildContext()
    {
        config::ConfigManager               mgr;
        auto                                ctx = std::make_unique<trdp_sim::EngineContext>();
        const auto                          configPath = buildTwoPdConfig();
        ctx->deviceConfig                    = mgr.loadDeviceConfigFromXml(configPath);
        mgr.validateDeviceConfig(ctx->deviceConfig);

        auto defs = mgr.buildDataSetDefs(ctx->deviceConfig);
        for (auto& def : defs)
        {
            ctx->dataSetDefs[def.id] = def;
            auto inst                = std::make_unique<data::DataSetInstance>();
            inst->def                = &ctx->dataSetDefs[def.id];
            inst->values.resize(def.elements.size());
            ctx->dataSetInstances[def.id] = std::move(inst);
        }
        return ctx;
    }

} // namespace

class PdSchedulingTest : public ::testing::Test
{
  protected:
    PdSchedulingTest() : ctx(buildContext()), adapter(*ctx), engine(*ctx, adapter) { ctx->pdEngine = &engine; }

    void SetUp() override { engine.initializeFromConfig(); }

    std::unique_ptr<trdp_sim::EngineContext> ctx;
    trdp_sim::trdp::TrdpAdapter              adapter;
    engine::pd::PdEngine                     engine;
};

TEST_F(PdSchedulingTest, OrdersPdByNextDueTime)
{
    auto now = std::chrono::steady_clock::now();
    for (auto& pdPtr : ctx->pdTelegrams)
    {
        if (!pdPtr || !pdPtr->cfg || pdPtr->direction != engine::pd::Direction::PUBLISH)
            continue;
        std::lock_guard<std::mutex> lk(pdPtr->mtx);
        auto cycle                 = std::chrono::microseconds(pdPtr->cfg->pdParam->cycleUs);
        pdPtr->stats.lastTxTime    = now - cycle;
        pdPtr->sendNow             = true;
        pdPtr->stats.lastSeqNumber = 0;
    }

    engine.processPublishersOnce(now);

    const auto log = adapter.getPdSendLog();
    ASSERT_GE(log.size(), 4u);
    EXPECT_EQ(log[0].comId, 100u);
    EXPECT_EQ(log[1].comId, 100u);
    EXPECT_EQ(log[2].comId, 101u);
    EXPECT_EQ(log[3].comId, 101u);
}

TEST_F(PdSchedulingTest, RedundantSendDropsChannelButContinues)
{
    {
        std::lock_guard<std::mutex> lk(ctx->simulation.mtx);
        ctx->simulation.redundancy.busFailure   = true;
        ctx->simulation.redundancy.failedChannel = 0;
    }
    auto now = std::chrono::steady_clock::now();
    for (auto& pdPtr : ctx->pdTelegrams)
    {
        if (!pdPtr || !pdPtr->cfg || pdPtr->direction != engine::pd::Direction::PUBLISH)
            continue;
        std::lock_guard<std::mutex> lk(pdPtr->mtx);
        auto cycle                 = std::chrono::microseconds(pdPtr->cfg->pdParam->cycleUs);
        pdPtr->stats.lastTxTime    = now - cycle;
        pdPtr->sendNow             = true;
        pdPtr->stats.busFailureDrops = 0;
    }

    engine.processPublishersOnce(now);

    const auto log = adapter.getPdSendLog();
    ASSERT_GE(log.size(), 2u);
    EXPECT_TRUE(log[0].dropped);
    EXPECT_FALSE(log[1].dropped);

    for (auto& pdPtr : ctx->pdTelegrams)
    {
        if (!pdPtr || !pdPtr->cfg || pdPtr->direction != engine::pd::Direction::PUBLISH)
            continue;
        std::lock_guard<std::mutex> lk(pdPtr->mtx);
        EXPECT_GE(pdPtr->stats.busFailureDrops, 1u);
        EXPECT_FALSE(pdPtr->stats.lastTxTime.time_since_epoch().count() == 0);
    }
}
