#include "engine_context.hpp"
#include "config_manager.hpp"
#include "trdp_adapter.hpp"
#include "pd_engine.hpp"
#include "md_engine.hpp"
#include "diagnostic_manager.hpp"
#include "backend_api.hpp"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>

using namespace drogon;

int main(int argc, char* argv[])
{
    std::string configPath = "config/trdp.xml";
    std::optional<bool> pcapEnableOverride;
    std::optional<std::string> pcapFileOverride;
    std::optional<std::size_t> pcapMaxSizeOverride;
    std::optional<std::size_t> pcapMaxFilesOverride;
    std::optional<bool> pcapRxOverride;
    std::optional<bool> pcapTxOverride;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--pcap-enable") {
            pcapEnableOverride = true;
        } else if (arg == "--pcap-disable") {
            pcapEnableOverride = false;
        } else if (arg == "--pcap-file" && i + 1 < argc) {
            pcapFileOverride = argv[++i];
        } else if (arg == "--pcap-max-size" && i + 1 < argc) {
            pcapMaxSizeOverride = std::stoull(argv[++i]);
        } else if (arg == "--pcap-max-files" && i + 1 < argc) {
            pcapMaxFilesOverride = std::stoull(argv[++i]);
        } else if (arg == "--pcap-rx-only") {
            pcapRxOverride = true;
            pcapTxOverride = false;
        } else if (arg == "--pcap-tx-only") {
            pcapTxOverride = true;
            pcapRxOverride = false;
        } else if (arg == "--pcap-bidirectional") {
            pcapTxOverride = true;
            pcapRxOverride = true;
        }
    }

    trdp_sim::EngineContext ctx;

    config::ConfigManager cfgMgr;
    ctx.deviceConfig = cfgMgr.loadDeviceConfigFromXml(configPath);
    cfgMgr.validateDeviceConfig(ctx.deviceConfig);

    // Build dataset defs & instances
    auto defs = cfgMgr.buildDataSetDefs(ctx.deviceConfig);
    for (auto& def : defs) {
        ctx.dataSetDefs[def.id] = def;
        auto inst = std::make_unique<data::DataSetInstance>();
        inst->def = &ctx.dataSetDefs[def.id];
        inst->values.resize(def.elements.size());
        ctx.dataSetInstances[def.id] = std::move(inst);
    }

    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    adapter.init();

    engine::pd::PdEngine pdEngine(ctx, adapter);
    engine::md::MdEngine mdEngine(ctx, adapter);

    ctx.pdEngine = &pdEngine;
    ctx.mdEngine = &mdEngine;

    diag::PcapConfig pcapCfg {};
    if (ctx.deviceConfig.pcap) {
        pcapCfg.enabled = ctx.deviceConfig.pcap->enabled;
        pcapCfg.captureTx = ctx.deviceConfig.pcap->captureTx;
        pcapCfg.captureRx = ctx.deviceConfig.pcap->captureRx;
        pcapCfg.filePath = ctx.deviceConfig.pcap->fileName;
        pcapCfg.maxFileSizeBytes = ctx.deviceConfig.pcap->maxSizeBytes;
        pcapCfg.maxFiles = ctx.deviceConfig.pcap->maxFiles;
    }
    if (pcapEnableOverride)
        pcapCfg.enabled = *pcapEnableOverride;
    if (pcapFileOverride)
        pcapCfg.filePath = *pcapFileOverride;
    if (pcapMaxSizeOverride)
        pcapCfg.maxFileSizeBytes = *pcapMaxSizeOverride;
    if (pcapMaxFilesOverride)
        pcapCfg.maxFiles = *pcapMaxFilesOverride;
    if (pcapRxOverride)
        pcapCfg.captureRx = *pcapRxOverride;
    if (pcapTxOverride)
        pcapCfg.captureTx = *pcapTxOverride;

    diag::DiagnosticManager diagMgr(ctx, pdEngine, mdEngine, adapter, {}, pcapCfg);
    ctx.diagManager = &diagMgr;
    diagMgr.start();

    pdEngine.initializeFromConfig();
    mdEngine.initializeFromConfig();

    pdEngine.start();
    mdEngine.start();

    api::BackendApi api(ctx, pdEngine, mdEngine, diagMgr);

    ctx.running = true;
    std::thread trdpThread([&]() {
        while (ctx.running) {
            adapter.processOnce();
        }
    });

    // ---------------- Drogon HTTP endpoints ----------------
    auto jsonResponse = [](const nlohmann::json& payload, drogon::HttpStatusCode code = k200OK) {
        auto resp = HttpResponse::newHttpJsonResponse(payload);
        resp->setStatusCode(code);
        return resp;
    };

    app().addListener("0.0.0.0", 8848);
    app().setThreadNum(std::max(2u, std::thread::hardware_concurrency()));

    // PD status
    app().registerHandler("/api/pd/status",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
            cb(jsonResponse(api.getPdStatus()));
        },
        {Get});

    // PD enable/disable
    app().registerHandler("/api/pd/{1}/enable",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb, uint32_t comId) {
            auto json = req->getJsonObject();
            if (!json || !json->contains("enabled")) {
                cb(jsonResponse({{"error", "missing 'enabled' flag"}}, k400BadRequest));
                return;
            }
            api.enablePdTelegram(comId, (*json)["enabled"].get<bool>());
            cb(jsonResponse(api.getPdStatus()));
        },
        {Post});

    // Dataset read
    app().registerHandler("/api/datasets/{1}",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb, uint32_t dataSetId) {
            cb(jsonResponse(api.getDataSetValues(dataSetId)));
        },
        {Get});

    // Dataset write/clear
    app().registerHandler("/api/datasets/{1}/elements/{2}",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb, uint32_t dataSetId, std::size_t elementIdx) {
            auto json = req->getJsonObject();
            if (!json) {
                cb(jsonResponse({{"error", "invalid JSON"}}, k400BadRequest));
                return;
            }
            if (json->value("clear", false)) {
                api.clearDataSetValue(dataSetId, elementIdx);
            } else if (json->contains("raw") && (*json)["raw"].is_array()) {
                std::vector<uint8_t> raw;
                for (const auto& v : (*json)["raw"]) {
                    raw.push_back(static_cast<uint8_t>(v.get<uint32_t>()));
                }
                api.setDataSetValue(dataSetId, elementIdx, raw);
            } else {
                cb(jsonResponse({{"error", "provide 'raw' array or set 'clear'"}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.getDataSetValues(dataSetId)));
        },
        {Post});

    app().registerHandler("/api/datasets/{1}/lock",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb, uint32_t dataSetId) {
            auto json = req->getJsonObject();
            if (!json || !json->contains("locked")) {
                cb(jsonResponse({{"error", "missing 'locked' flag"}}, k400BadRequest));
                return;
            }
            api.lockDataSet(dataSetId, (*json)["locked"].get<bool>());
            cb(jsonResponse(api.getDataSetValues(dataSetId)));
        },
        {Post});

    // Config summary & reload
    app().registerHandler("/api/config",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
            cb(jsonResponse(api.getConfigSummary()));
        },
        {Get});

    app().registerHandler("/api/config/reload",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            auto json = req->getJsonObject();
            if (!json || !json->contains("path")) {
                cb(jsonResponse({{"error", "missing 'path'"}}, k400BadRequest));
                return;
            }
            api.reloadConfiguration((*json)["path"].get<std::string>());
            cb(jsonResponse(api.getConfigSummary()));
        },
        {Post});

    // MD session status
    app().registerHandler("/api/md/session/{1}",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb, uint32_t sessionId) {
            cb(jsonResponse(api.getMdSessionStatus(sessionId)));
        },
        {Get});

    app().registerHandler("/api/md/{1}/request",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb, uint32_t comId) {
            uint32_t sessionId = api.createMdRequest(comId);
            if (sessionId == 0) {
                cb(jsonResponse({{"error", "failed to create session"}}, k400BadRequest));
                return;
            }
            api.sendMdRequest(sessionId);
            cb(jsonResponse(api.getMdSessionStatus(sessionId)));
        },
        {Post});

    // Diagnostics
    app().registerHandler("/api/diag/events",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            auto maxStr = req->getParameter("max");
            std::size_t maxEvents = maxStr.empty() ? 50u : static_cast<std::size_t>(std::stoul(maxStr));
            cb(jsonResponse(api.getRecentEvents(maxEvents)));
        },
        {Get});

    app().registerHandler("/api/diag/metrics",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
            cb(jsonResponse(api.getDiagnosticsMetrics()));
        },
        {Get});

    app().registerHandler("/api/diag/event",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            auto json = req->getJsonObject();
            if (!json || !json->contains("component") || !json->contains("message")) {
                cb(jsonResponse({{"error", "component and message required"}}, k400BadRequest));
                return;
            }
            auto severityStr = json->value("severity", std::string("INFO"));
            api.triggerDiagnosticEvent(severityStr, (*json)["component"].get<std::string>(), (*json)["message"].get<std::string>());
            cb(jsonResponse({{"status", "queued"}}));
        },
        {Post});

    // ---------------- Run Drogon ----------------
    app().run();

    // Cleanup
    ctx.running = false;
    if (trdpThread.joinable())
        trdpThread.join();
    pdEngine.stop();
    mdEngine.stop();
    adapter.deinit();
    diagMgr.stop();

    return 0;
}
