#include "backend_api.hpp"
#include "diagnostic_manager.hpp"
#include "engine_context.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <filesystem>
#include <gtest/gtest.h>

namespace
{

    struct TestHarness
    {
        trdp_sim::EngineContext     ctx;
        trdp_sim::trdp::TrdpAdapter adapter;
        engine::pd::PdEngine        pd;
        engine::md::MdEngine        md;
        diag::DiagnosticManager     diagMgr;
        trdp_sim::BackendEngine     backend;
        api::BackendApi             api;

        TestHarness()
            : adapter(ctx)
            , pd(ctx, adapter)
            , md(ctx, adapter)
            , diagMgr(ctx, pd, md, adapter)
            , backend(ctx, pd, md, diagMgr)
            , api(ctx, backend, pd, md, adapter, diagMgr)
        {
            ctx.diagManager = &diagMgr;
        }
    };

    std::filesystem::path tmpFile(const std::string& name)
    {
        auto dir = std::filesystem::temp_directory_path() / "trdp-export-tests";
        std::filesystem::create_directories(dir);
        return dir / name;
    }

} // namespace

TEST(DiagnosticExportCycle, WritesTextAndJson)
{
    TestHarness harness;
    harness.diagMgr.log(diag::Severity::INFO, "test", "hello");
    harness.diagMgr.log(diag::Severity::ERROR, "test", "fail");

    auto textOut = tmpFile("events.txt");
    auto jsonOut = tmpFile("events.json");

    EXPECT_TRUE(harness.api.exportRecentEventsToFile(10, false, textOut));
    EXPECT_TRUE(harness.api.exportRecentEventsToFile(10, true, jsonOut));
    EXPECT_TRUE(std::filesystem::exists(textOut));
    EXPECT_TRUE(std::filesystem::exists(jsonOut));
}

TEST(DiagnosticExportCycle, CopiesPcapCapture)
{
    diag::PcapConfig cfg{};
    cfg.enabled  = true;
    cfg.filePath = tmpFile("capture.pcap").string();

    TestHarness harness;
    harness.diagMgr.updatePcapConfig(cfg);

    std::vector<uint8_t> payload(32, 0xAB);
    harness.diagMgr.writePacketToPcap(payload.data(), payload.size(), true);

    auto destination = tmpFile("capture_copy.pcap");
    EXPECT_TRUE(harness.api.exportPcapCapture(destination));
    EXPECT_TRUE(std::filesystem::exists(destination));
}
