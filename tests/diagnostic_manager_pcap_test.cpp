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

        explicit TestHarness(const diag::PcapConfig& cfg)
            : adapter(ctx), pd(ctx, adapter), md(ctx, adapter), diagMgr(ctx, pd, md, adapter, {}, cfg)
        {
            ctx.diagManager = &diagMgr;
        }
    };

    std::filesystem::path makeTempPath(const std::string& name)
    {
        auto base = std::filesystem::temp_directory_path() / "trdp-pcap-tests";
        std::filesystem::create_directories(base);
        return base / name;
    }

} // namespace

TEST(DiagnosticManagerPcapTest, WritesPackets)
{
    auto pcapPath = makeTempPath("capture.pcap");
    std::filesystem::remove(pcapPath);

    diag::PcapConfig cfg{};
    cfg.enabled          = true;
    cfg.filePath         = pcapPath.string();
    cfg.maxFileSizeBytes = 0;

    TestHarness          harness(cfg);
    std::vector<uint8_t> payload(64, 0xAA);
    harness.diagMgr.writePacketToPcap(payload.data(), payload.size(), true);
    harness.diagMgr.writePacketToPcap(payload.data(), payload.size(), false);

    harness.diagMgr.stop();

    ASSERT_TRUE(std::filesystem::exists(pcapPath));
    EXPECT_GT(std::filesystem::file_size(pcapPath), static_cast<uintmax_t>(payload.size()));
}

TEST(DiagnosticManagerPcapTest, RotatesWhenConfigured)
{
    auto pcapPath    = makeTempPath("rotate.pcap");
    auto rotatedPath = std::filesystem::path(pcapPath.string() + ".1");
    std::filesystem::remove(pcapPath);
    std::filesystem::remove(rotatedPath);

    diag::PcapConfig cfg{};
    cfg.enabled          = true;
    cfg.filePath         = pcapPath.string();
    cfg.maxFileSizeBytes = 200;
    cfg.maxFiles         = 2;

    TestHarness          harness(cfg);
    std::vector<uint8_t> payload(150, 0xBB);
    harness.diagMgr.writePacketToPcap(payload.data(), payload.size(), true);
    harness.diagMgr.writePacketToPcap(payload.data(), payload.size(), true);
    harness.diagMgr.writePacketToPcap(payload.data(), payload.size(), true);

    harness.diagMgr.stop();

    EXPECT_TRUE(std::filesystem::exists(pcapPath));
    EXPECT_TRUE(std::filesystem::exists(rotatedPath));
}
