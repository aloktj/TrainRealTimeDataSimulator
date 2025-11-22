#include "backend_api.hpp"
#include "backend_engine.hpp"
#include "diagnostic_manager.hpp"
#include "engine_context.hpp"
#include "auth_manager.hpp"
#include "realtime_hub.hpp"
#include "realtime_stream.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"
#include "xml_loader.hpp"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

using namespace drogon;

int main(int argc, char* argv[])
{
    std::string                configPath = "config/trdp.xml";
    std::optional<bool>        pcapEnableOverride;
    std::optional<std::string> pcapFileOverride;
    std::optional<std::size_t> pcapMaxSizeOverride;
    std::optional<std::size_t> pcapMaxFilesOverride;
    std::optional<bool>        pcapRxOverride;
    std::optional<bool>        pcapTxOverride;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
        {
            configPath = argv[++i];
        }
        else if (arg == "--pcap-enable")
        {
            pcapEnableOverride = true;
        }
        else if (arg == "--pcap-disable")
        {
            pcapEnableOverride = false;
        }
        else if (arg == "--pcap-file" && i + 1 < argc)
        {
            pcapFileOverride = argv[++i];
        }
        else if (arg == "--pcap-max-size" && i + 1 < argc)
        {
            pcapMaxSizeOverride = std::stoull(argv[++i]);
        }
        else if (arg == "--pcap-max-files" && i + 1 < argc)
        {
            pcapMaxFilesOverride = std::stoull(argv[++i]);
        }
        else if (arg == "--pcap-rx-only")
        {
            pcapRxOverride = true;
            pcapTxOverride = false;
        }
        else if (arg == "--pcap-tx-only")
        {
            pcapTxOverride = true;
            pcapRxOverride = false;
        }
        else if (arg == "--pcap-bidirectional")
        {
            pcapTxOverride = true;
            pcapRxOverride = true;
        }
    }

    trdp_sim::EngineContext ctx;

    config::XmlConfigurationLoader xmlLoader;
    ctx.deviceConfig = xmlLoader.load(configPath);
    ctx.configPath   = configPath;

    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    adapter.init();

    engine::pd::PdEngine pdEngine(ctx, adapter);
    engine::md::MdEngine mdEngine(ctx, adapter);

    ctx.pdEngine = &pdEngine;
    ctx.mdEngine = &mdEngine;
    ctx.trdpAdapter = &adapter;

    diag::PcapConfig pcapCfg{};
    if (ctx.deviceConfig.pcap)
    {
        pcapCfg.enabled          = ctx.deviceConfig.pcap->enabled;
        pcapCfg.captureTx        = ctx.deviceConfig.pcap->captureTx;
        pcapCfg.captureRx        = ctx.deviceConfig.pcap->captureRx;
        pcapCfg.filePath         = ctx.deviceConfig.pcap->fileName;
        pcapCfg.maxFileSizeBytes = ctx.deviceConfig.pcap->maxSizeBytes;
        pcapCfg.maxFiles         = ctx.deviceConfig.pcap->maxFiles;
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

    trdp_sim::BackendEngine backend(ctx, pdEngine, mdEngine, diagMgr);
    backend.applyPreloadedConfiguration(ctx.deviceConfig);

    api::BackendApi api(ctx, backend, pdEngine, mdEngine, adapter, diagMgr);

    auth::AuthManager        authMgr;
    realtime::RealtimeHub    hub(ctx, api, diagMgr, authMgr);
    RealtimeStream::bootstrap(&authMgr, &hub);
    hub.start();

    ctx.running = true;
    std::thread trdpThread(
        [&]()
        {
            while (ctx.running)
            {
                adapter.processOnce();
            }
        });

    // ---------------- Drogon HTTP endpoints ----------------
    auto jsonResponse = [](const nlohmann::json& payload, drogon::HttpStatusCode code = k200OK)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(code);
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(payload.dump());
        return resp;
    };

    auto extractToken = [](const HttpRequestPtr& req)
    {
        std::string token;
        auto        authHeader = req->getHeader("Authorization");
        if (!authHeader.empty())
        {
            const std::string bearer = "Bearer ";
            if (authHeader.rfind(bearer, 0) == 0)
                token = authHeader.substr(bearer.size());
        }
        if (token.empty())
            token = req->getCookie("trdp_session");
        return token;
    };

    app().addListener("0.0.0.0", 8848);
    app().setThreadNum(std::max(2u, std::thread::hardware_concurrency()));

    // Auth login
    app().registerHandler(
        "/api/auth/login",
        [&authMgr, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("username") || !json->isMember("password"))
            {
                cb(jsonResponse({{"error", "username/password required"}}, k400BadRequest));
                return;
            }
            auto session = authMgr.login((*json)["username"].asString(), (*json)["password"].asString());
            if (!session)
            {
                cb(jsonResponse({{"error", "invalid credentials"}}, k401Unauthorized));
                return;
            }
            auto resp = jsonResponse({{"token", session->token}, {"role", auth::roleToString(session->role)},
                                       {"theme", session->theme}},
                                     k200OK);
            resp->addCookie("trdp_session", session->token);
            cb(resp);
        },
        {Post});

    // Auth logout
    app().registerHandler(
        "/api/auth/logout",
        [&authMgr, extractToken, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto token = extractToken(req);
            if (!token.empty())
                authMgr.logout(token);
            cb(jsonResponse({{"status", "ok"}}, k200OK));
        },
        {Post});

    // Auth session
    app().registerHandler(
        "/api/auth/session",
        [&authMgr, extractToken, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto token   = extractToken(req);
            auto session = authMgr.validate(token);
            if (!session)
            {
                cb(jsonResponse({{"error", "unauthorized"}}, k401Unauthorized));
                return;
            }
            cb(jsonResponse({{"username", session->username}, {"role", auth::roleToString(session->role)},
                             {"theme", session->theme}},
                            k200OK));
        },
        {Get});

    // Theme update
    app().registerHandler(
        "/api/ui/theme",
        [&authMgr, extractToken, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto token   = extractToken(req);
            auto session = authMgr.validate(token);
            if (!session)
            {
                cb(jsonResponse({{"error", "unauthorized"}}, k401Unauthorized));
                return;
            }
            auto json = req->getJsonObject();
            if (!json || !json->isMember("theme"))
            {
                cb(jsonResponse({{"error", "theme required"}}, k400BadRequest));
                return;
            }
            authMgr.updateTheme(token, (*json)["theme"].asString());
            cb(jsonResponse({{"theme", (*json)["theme"].asString()}}, k200OK));
        },
        {Post});

    // Layout manifest for dashboards/panels
    app().registerHandler(
        "/api/ui/layout",
        [&jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            nlohmann::json layout{{"panels", nlohmann::json::array()}};
            layout["panels"].push_back({{"id", "pd"}, {"title", "PD Dashboard"}, {"features", {"live"}}});
            layout["panels"].push_back({{"id", "md"}, {"title", "MD Dashboard"}, {"features", {"sessions"}}});
            layout["panels"].push_back({{"id", "datasets"}, {"title", "Dataset Editor"}, {"features", {"edit"}}});
            layout["panels"].push_back({{"id", "xml"}, {"title", "XML Visual Viewer"}, {"features", {"tree"}}});
            layout["panels"].push_back({{"id", "logs"}, {"title", "Log Viewer"}, {"features", {"stream"}}});
            layout["panels"].push_back({{"id", "interfaces"}, {"title", "Interface Diagnostics"},
                                         {"features", {"qos", "redundancy"}}});
            layout["panels"].push_back({{"id", "theme"}, {"title", "Theme Switch"}, {"features", {"dark", "light"}}});
            cb(jsonResponse(layout, k200OK));
        },
        {Get});

    // UI overview snapshot
    app().registerHandler(
        "/api/ui/overview",
        [&api, &diagMgr, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            nlohmann::json snap;
            snap["pd"]      = api.getPdStatus();
            snap["metrics"] = api.getDiagnosticsMetrics();
            snap["config"]  = api.getConfigSummary();
            snap["events"]  = api.getRecentEvents(25);
            cb(jsonResponse(snap, k200OK));
        },
        {Get});

    // PD status
    app().registerHandler("/api/pd/status",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.getPdStatus())); }, {Get});

    // PD enable/disable
    app().registerHandler("/api/pd/{1}/enable",
                          [&api, jsonResponse](const HttpRequestPtr&                         req,
                                               std::function<void(const HttpResponsePtr&)>&& cb, uint32_t comId)
                          {
                              auto json = req->getJsonObject();
                              if (!json || !json->isMember("enabled"))
                              {
                                  cb(jsonResponse({{"error", "missing 'enabled' flag"}}, k400BadRequest));
                                  return;
                              }
                              api.enablePdTelegram(comId, (*json)["enabled"].asBool());
                              cb(jsonResponse(api.getPdStatus()));
                          },
                          {Post});

    // Dataset read
    app().registerHandler("/api/datasets/{1}",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb,
                                               uint32_t dataSetId)
                          { cb(jsonResponse(api.getDataSetValues(dataSetId))); },
                          {Get});

    // Dataset write/clear
    app().registerHandler("/api/datasets/{1}/elements/{2}",
                          [&api, jsonResponse](const HttpRequestPtr&                         req,
                                               std::function<void(const HttpResponsePtr&)>&& cb, uint32_t dataSetId,
                                               std::size_t elementIdx)
                          {
                              auto json = req->getJsonObject();
                              if (!json)
                              {
                                  cb(jsonResponse({{"error", "invalid JSON"}}, k400BadRequest));
                                  return;
                              }
                              std::string error;
                              if (json->get("clear", false).asBool())
                              {
                                  if (!api.clearDataSetValue(dataSetId, elementIdx, &error))
                                  {
                                      cb(jsonResponse({{"error", error}}, k400BadRequest));
                                      return;
                                  }
                              }
                              else if (json->isMember("raw") && (*json)["raw"].isArray())
                              {
                                  std::vector<uint8_t> raw;
                                  for (const auto& v : (*json)["raw"])
                                  {
                                      if (!v.isUInt() || v.asUInt() > 255)
                                      {
                                          cb(jsonResponse({{"error", "raw values must be uint8"}}, k400BadRequest));
                                          return;
                                      }
                                      raw.push_back(static_cast<uint8_t>(v.asUInt()));
                                  }
                                  if (!api.setDataSetValue(dataSetId, elementIdx, raw, &error))
                                  {
                                      cb(jsonResponse({{"error", error}}, k400BadRequest));
                                      return;
                                  }
                              }
                              else
                              {
                                  cb(jsonResponse({{"error", "provide 'raw' array or set 'clear'"}}, k400BadRequest));
                                  return;
                              }
                              cb(jsonResponse(api.getDataSetValues(dataSetId)));
                          },
                          {Post});

    app().registerHandler("/api/datasets/{1}/lock",
                          [&api, jsonResponse](const HttpRequestPtr&                         req,
                                               std::function<void(const HttpResponsePtr&)>&& cb, uint32_t dataSetId)
                          {
                              auto json = req->getJsonObject();
                              if (!json || !json->isMember("locked"))
                              {
                                  cb(jsonResponse({{"error", "missing 'locked' flag"}}, k400BadRequest));
                                  return;
                              }
                              std::string error;
                              if (!api.lockDataSet(dataSetId, (*json)["locked"].asBool(), &error))
                              {
                                  cb(jsonResponse({{"error", error}}, k400BadRequest));
                                  return;
                              }
                              cb(jsonResponse(api.getDataSetValues(dataSetId)));
                          },
                          {Post});

    app().registerHandler(
        "/api/datasets/{1}/clear_all",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb,
                             uint32_t dataSetId)
        {
            std::string error;
            if (!api.clearAllDataSetValues(dataSetId, &error))
            {
                cb(jsonResponse({{"error", error}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.getDataSetValues(dataSetId)));
        },
        {Post});

    // Config summary & reload
    app().registerHandler("/api/config",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.getConfigSummary())); }, {Get});

    app().registerHandler("/api/config/detail",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.getConfigDetail())); }, {Get});

    app().registerHandler(
        "/api/config/reload",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "missing 'path'"}}, k400BadRequest));
                return;
            }
            try
            {
                api.reloadConfiguration((*json)["path"].asString());
                cb(jsonResponse(api.getConfigSummary()));
            }
            catch (const config::ConfigError& ex)
            {
                cb(jsonResponse({{"error", ex.what()}, {"line", ex.line()}, {"file", ex.file()}}, k400BadRequest));
            }
            catch (const std::exception& ex)
            {
                cb(jsonResponse({{"error", ex.what()}}, k400BadRequest));
            }
        },
        {Post});

    app().registerHandler(
        "/api/config/backup",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto path = req->getParameter("path");
            if (!path.empty())
            {
                if (!api.backupConfiguration(path))
                {
                    cb(jsonResponse({{"error", "backup failed"}}, k500InternalServerError));
                    return;
                }
                cb(jsonResponse({{"backup", path}}));
                return;
            }

            auto cfgPath = api.getConfigPath();
            if (cfgPath && std::filesystem::exists(*cfgPath))
            {
                auto resp = HttpResponse::newFileResponse(cfgPath->string());
                resp->addHeader("Content-Disposition", "attachment; filename=trdp_config_backup.xml");
                cb(resp);
                return;
            }
            cb(jsonResponse({{"error", "no configuration path"}}, k400BadRequest));
        },
        {Get});

    app().registerHandler(
        "/api/config/restore",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "missing 'path'"}}, k400BadRequest));
                return;
            }
            if (!api.restoreConfiguration((*json)["path"].asString()))
            {
                cb(jsonResponse({{"error", "restore failed"}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.getConfigSummary()));
        },
        {Post});

    app().registerHandler("/api/network/multicast",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.getMulticastStatus())); }, {Get});

    app().registerHandler(
        "/api/network/multicast/join",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("interface") || !json->isMember("group"))
            {
                cb(jsonResponse({{"error", "'interface' and 'group' required"}}, k400BadRequest));
                return;
            }
            std::optional<std::string> nic;
            if (json->isMember("nic"))
                nic = (*json)["nic"].asString();
            if (!api.joinMulticastGroup((*json)["interface"].asString(), (*json)["group"].asString(), nic))
            {
                cb(jsonResponse({{"error", "join failed"}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.getMulticastStatus()));
        },
        {Post});

    app().registerHandler(
        "/api/network/multicast/leave",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("interface") || !json->isMember("group"))
            {
                cb(jsonResponse({{"error", "'interface' and 'group' required"}}, k400BadRequest));
                return;
            }
            if (!api.leaveMulticastGroup((*json)["interface"].asString(), (*json)["group"].asString()))
            {
                cb(jsonResponse({{"error", "leave failed or group not joined"}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.getMulticastStatus()));
        },
        {Post});

    // MD session status
    app().registerHandler("/api/md/session/{1}",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb,
                                               uint32_t sessionId)
                          { cb(jsonResponse(api.getMdSessionStatus(sessionId))); },
                          {Get});

    app().registerHandler("/api/md/{1}/request",
                          [&api, jsonResponse]([[maybe_unused]] const HttpRequestPtr&        req,
                                               std::function<void(const HttpResponsePtr&)>&& cb, uint32_t comId)
                          {
                              uint32_t sessionId = api.createMdRequest(comId);
                              if (sessionId == 0)
                              {
                                  cb(jsonResponse({{"error", "failed to create session"}}, k400BadRequest));
                                  return;
                              }
                              api.sendMdRequest(sessionId);
                              cb(jsonResponse(api.getMdSessionStatus(sessionId)));
                          },
                          {Post});

    // Diagnostics
    app().registerHandler(
        "/api/diag/events",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto        maxStr    = req->getParameter("max");
            std::size_t maxEvents = maxStr.empty() ? 50u : static_cast<std::size_t>(std::stoul(maxStr));
            cb(jsonResponse(api.getRecentEvents(maxEvents)));
        },
        {Get});

    app().registerHandler(
        "/api/diag/log/export",
        [&api](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto        maxStr    = req->getParameter("max");
            std::size_t maxEvents = maxStr.empty() ? 200u : static_cast<std::size_t>(std::stoul(maxStr));
            auto        format    = req->getParameter("format");
            if (format == "json")
            {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(api.getRecentEvents(maxEvents).dump());
                cb(resp);
                return;
            }

            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_TEXT_PLAIN);
            resp->setBody(api.exportRecentEventsText(maxEvents));
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/diag/pcap/export",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto path = api.getPcapCapturePath();
            if (!path || !std::filesystem::exists(*path))
            {
                cb(jsonResponse({{"error", "pcap not available"}}, k404NotFound));
                return;
            }
            auto resp = HttpResponse::newFileResponse(path->string());
            resp->addHeader("Content-Disposition", "attachment; filename=trdp_capture.pcap");
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/diag/log/file",
        [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto path = api.getLogFilePath();
            if (!path || !std::filesystem::exists(*path))
            {
                cb(jsonResponse({{"error", "log not available"}}, k404NotFound));
                return;
            }
            auto resp = HttpResponse::newFileResponse(path->string());
            resp->addHeader("Content-Disposition", "attachment; filename=trdp_logs.txt");
            cb(resp);
        },
        {Get});

    app().registerHandler("/api/diag/metrics",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.getDiagnosticsMetrics())); }, {Get});

    app().registerHandler(
        "/api/diag/event",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("component") || !json->isMember("message"))
            {
                cb(jsonResponse({{"error", "component and message required"}}, k400BadRequest));
                return;
            }
            auto severityStr = json->get("severity", "INFO").asString();
            api.triggerDiagnosticEvent(severityStr, (*json)["component"].asString(), (*json)["message"].asString());
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
