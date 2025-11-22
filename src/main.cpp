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
#include <cctype>
#include <cstdlib>
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

    auto sanitizeBoundedText = [](std::string input, std::size_t maxLen)
    {
        input.erase(std::remove_if(input.begin(), input.end(), [](unsigned char c) { return std::iscntrl(c); }),
                    input.end());
        if (input.size() > maxLen)
            input.resize(maxLen);
        return input;
    };

    auto extractToken = [&](const HttpRequestPtr& req)
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
        return sanitizeBoundedText(token, 128);
    };

    auto roleAtLeast = [](auth::Role current, auth::Role required)
    {
        if (current == auth::Role::Admin)
            return true;
        if (current == auth::Role::Developer)
            return required == auth::Role::Developer || required == auth::Role::Viewer;
        return required == auth::Role::Viewer;
    };

    auto requireRole = [&](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>& cb, auth::Role required)
        -> std::optional<auth::Session>
    {
        auto token   = extractToken(req);
        auto session = authMgr.validate(token);
        if (!session)
        {
            cb(jsonResponse({{"error", "unauthorized"}}, k401Unauthorized));
            return std::nullopt;
        }
        if (!roleAtLeast(session->role, required))
        {
            cb(jsonResponse({{"error", "forbidden"}}, k403Forbidden));
            return std::nullopt;
        }
        return session;
    };

    auto sanitizePath = [](const std::string& raw) -> std::optional<std::filesystem::path>
    {
        try
        {
            auto normalized = std::filesystem::weakly_canonical(std::filesystem::absolute(raw));
            auto cwd        = std::filesystem::current_path();
            if (normalized.string().rfind(cwd.string(), 0) != 0)
                return std::nullopt;
            return normalized;
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    };

    auto getEnvOrDefault = [](const std::string& key, const std::string& def) {
        const char* val = std::getenv(key.c_str());
        if (val && *val)
            return std::string(val);
        return def;
    };

    const std::string bindHost = getEnvOrDefault("TRDP_HTTP_HOST", "127.0.0.1");
    const uint16_t    bindPort = static_cast<uint16_t>(std::stoi(getEnvOrDefault("TRDP_HTTP_PORT", "8848")));

    app().addListener(bindHost, bindPort);
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
            const auto username = (*json)["username"].asString();
            const auto password = (*json)["password"].asString();
            if (username.size() > 64 || password.size() > 256)
            {
                cb(jsonResponse({{"error", "credentials too long"}}, k400BadRequest));
                return;
            }
            auto session = authMgr.login(username, password);
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
            auto session = requireRole(req, cb, auth::Role::Viewer);
            if (!session)
                return;
            cb(jsonResponse({{"username", session->get().username}, {"role", auth::roleToString(session->get().role)},
                             {"theme", session->get().theme}},
                            k200OK));
        },
        {Get});

    // Theme update
    app().registerHandler(
        "/api/ui/theme",
        [&authMgr, extractToken, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto session = requireRole(req, cb, auth::Role::Viewer);
            if (!session)
                return;
            auto json = req->getJsonObject();
            if (!json || !json->isMember("theme"))
            {
                cb(jsonResponse({{"error", "theme required"}}, k400BadRequest));
                return;
            }
            authMgr.updateTheme(session->get().token, (*json)["theme"].asString());
            cb(jsonResponse({{"theme", (*json)["theme"].asString()}}, k200OK));
        },
        {Post});

    // Layout manifest for dashboards/panels
    app().registerHandler(
        "/api/ui/layout",
        [&requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Viewer))
                return;
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
        [&api, &diagMgr, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Viewer))
                return;
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
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb)
                          {
                              if (!requireRole(req, cb, auth::Role::Viewer))
                                  return;
                              cb(jsonResponse(api.getPdStatus()));
                          },
                          {Get});

    // PD enable/disable
    app().registerHandler("/api/pd/{1}/enable",
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                            std::function<void(const HttpResponsePtr&)>&& cb,
                                                            uint32_t comId)
                          {
                              if (!requireRole(req, cb, auth::Role::Developer))
                                  return;
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
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb,
                                                             uint32_t dataSetId)
                          {
                              if (!requireRole(req, cb, auth::Role::Viewer))
                                  return;
                              cb(jsonResponse(api.getDataSetValues(dataSetId)));
                          },
                          {Get});

    // Dataset write/clear
    app().registerHandler("/api/datasets/{1}/elements/{2}",
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb,
                                                             uint32_t dataSetId, std::size_t elementIdx)
                          {
                              if (!requireRole(req, cb, auth::Role::Developer))
                                  return;
                              auto json = req->getJsonObject();
                              if (!json)
                              {
                                  cb(jsonResponse({{"error", "invalid JSON"}}, k400BadRequest));
                                  return;
                              }
                              std::string error;
                              auto        expectedSize = api.getExpectedElementSize(dataSetId, elementIdx);
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
                                  const auto rawArraySize = (*json)["raw"].size();
                                  if (rawArraySize > 65536)
                                  {
                                      cb(jsonResponse({{"error", "raw payload too large"}}, k400BadRequest));
                                      return;
                                  }
                                  if (expectedSize && rawArraySize != *expectedSize)
                                  {
                                      cb(jsonResponse({{"error", "raw payload length mismatch"}}, k400BadRequest));
                                      return;
                                  }
                                  std::vector<uint8_t> raw;
                                  raw.reserve(rawArraySize);
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
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb,
                                                             uint32_t dataSetId)
                          {
                              if (!requireRole(req, cb, auth::Role::Developer))
                                  return;
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
        [&api, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb,
                                           uint32_t dataSetId)
        {
            if (!requireRole(req, cb, auth::Role::Developer))
                return;
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
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb)
                          {
                              if (!requireRole(req, cb, auth::Role::Viewer))
                                  return;
                              cb(jsonResponse(api.getConfigSummary()));
                          },
                          {Get});

    app().registerHandler("/api/config/detail",
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb)
                          {
                              if (!requireRole(req, cb, auth::Role::Viewer))
                                  return;
                              cb(jsonResponse(api.getConfigDetail()));
                          },
                          {Get});

    app().registerHandler(
        "/api/config/reload",
        [&api, &requireRole, &sanitizePath, jsonResponse](const HttpRequestPtr& req,
                                                          std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Admin))
                return;
            auto json = req->getJsonObject();
            if (!json || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "missing 'path'"}}, k400BadRequest));
                return;
            }
            auto sanitized = sanitizePath((*json)["path"].asString());
            if (!sanitized)
            {
                cb(jsonResponse({{"error", "invalid path"}}, k400BadRequest));
                return;
            }
            try
            {
                api.reloadConfiguration(sanitized->string());
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
        [&api, &requireRole, &sanitizePath, jsonResponse](const HttpRequestPtr& req,
                                                          std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Admin))
                return;
            auto path = req->getParameter("path");
            if (!path.empty())
            {
                auto sanitized = sanitizePath(path);
                if (!sanitized || !api.backupConfiguration(*sanitized))
                {
                    cb(jsonResponse({{"error", "backup failed"}}, k500InternalServerError));
                    return;
                }
                cb(jsonResponse({{"backup", sanitized->string()}}));
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
        [&api, &requireRole, &sanitizePath, jsonResponse](const HttpRequestPtr& req,
                                                          std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Admin))
                return;
            auto json = req->getJsonObject();
            if (!json || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "missing 'path'"}}, k400BadRequest));
                return;
            }
            auto path = sanitizePath((*json)["path"].asString());
            if (!path || !api.restoreConfiguration(*path))
            {
                cb(jsonResponse({{"error", "restore failed"}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.getConfigSummary()));
        },
        {Post});

    app().registerHandler("/api/network/multicast",
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb)
                          {
                              if (!requireRole(req, cb, auth::Role::Viewer))
                                  return;
                              cb(jsonResponse(api.getMulticastStatus()));
                          },
                          {Get});

    app().registerHandler(
        "/api/network/multicast/join",
        [&api, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Developer))
                return;
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
        [&api, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Developer))
                return;
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

    // Simulation controls
    app().registerHandler(
        "/api/sim/injection",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (json && json->get("clear", false).asBool())
            {
                api.clearInjectionRules();
                cb(jsonResponse(api.getSimulationState()));
                return;
            }
            if (!json || !json->isMember("type") || !json->isMember("id"))
            {
                cb(jsonResponse({{"error", "'type' and 'id' required"}}, k400BadRequest));
                return;
            }
            trdp_sim::SimulationControls::InjectionRule rule{};
            if (json->isMember("corruptComId"))
                rule.corruptComId = (*json)["corruptComId"].asBool();
            if (json->isMember("corruptDataSet"))
                rule.corruptDataSetId = (*json)["corruptDataSet"].asBool();
            if (json->isMember("seqDelta"))
                rule.seqDelta = (*json)["seqDelta"].asInt();
            if (json->isMember("delayMs"))
                rule.delayMs = static_cast<uint32_t>((*json)["delayMs"].asUInt());
            if (json->isMember("lossRate"))
                rule.lossRate = (*json)["lossRate"].asDouble();

            auto type = (*json)["type"].asString();
            auto id   = (*json)["id"].asUInt();
            if (type == "pd")
                api.upsertPdInjectionRule(id, rule);
            else if (type == "md")
                api.upsertMdInjectionRule(id, rule);
            else if (type == "dataset")
                api.upsertDataSetInjectionRule(id, rule);
            else
            {
                cb(jsonResponse({{"error", "type must be pd, md, or dataset"}}, k400BadRequest));
                return;
            }

            cb(jsonResponse(api.getSimulationState()));
        },
        {Post});

    app().registerHandler("/api/sim/state",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.getSimulationState())); },
                          {Get});

    app().registerHandler(
        "/api/sim/stress",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json)
            {
                cb(jsonResponse({{"error", "invalid payload"}}, k400BadRequest));
                return;
            }
            trdp_sim::SimulationControls::StressMode mode{};
            mode.enabled          = json->get("enabled", false).asBool();
            mode.pdCycleOverrideUs = json->get("pdCycleUs", 0).asUInt();
            mode.mdBurst          = json->get("mdBurst", 0).asUInt();
            mode.mdIntervalUs     = json->get("mdIntervalUs", 0).asUInt();
            api.setStressMode(mode);
            cb(jsonResponse(api.getSimulationState()));
        },
        {Post});

    app().registerHandler(
        "/api/sim/redundancy",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json)
            {
                cb(jsonResponse({{"error", "invalid payload"}}, k400BadRequest));
                return;
            }
            trdp_sim::SimulationControls::RedundancySimulation sim{};
            sim.forceSwitch  = json->get("forceSwitch", false).asBool();
            sim.busFailure   = json->get("busFailure", false).asBool();
            sim.failedChannel = json->get("failedChannel", 0).asUInt();
            api.setRedundancySimulation(sim);
            cb(jsonResponse(api.getSimulationState()));
        },
        {Post});

    app().registerHandler(
        "/api/sim/time",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json)
            {
                cb(jsonResponse({{"error", "invalid payload"}}, k400BadRequest));
                return;
            }
            trdp_sim::SimulationControls::TimeSyncOffsets offsets{};
            offsets.ntpOffsetUs = json->get("ntpOffsetUs", 0).asInt64();
            offsets.ptpOffsetUs = json->get("ptpOffsetUs", 0).asInt64();
            api.setTimeSyncOffsets(offsets);
            cb(jsonResponse(api.getSimulationState()));
        },
        {Post});

    app().registerHandler("/api/sim/instances",
                          [&api, jsonResponse](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
                          { cb(jsonResponse(api.listVirtualInstances())); },
                          {Get});

    app().registerHandler(
        "/api/sim/instances/register",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("name") || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "name and path required"}}, k400BadRequest));
                return;
            }
            std::string err;
            if (!api.registerVirtualInstance((*json)["name"].asString(), (*json)["path"].asString(), &err))
            {
                cb(jsonResponse({{"error", err}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.listVirtualInstances()));
        },
        {Post});

    app().registerHandler(
        "/api/sim/instances/activate",
        [&api, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("name"))
            {
                cb(jsonResponse({{"error", "name required"}}, k400BadRequest));
                return;
            }
            std::string err;
            if (!api.activateVirtualInstance((*json)["name"].asString(), &err))
            {
                cb(jsonResponse({{"error", err}}, k400BadRequest));
                return;
            }
            cb(jsonResponse(api.listVirtualInstances()));
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
        [&api, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Viewer))
                return;
            auto        maxStr    = req->getParameter("max");
            std::size_t maxEvents = maxStr.empty() ? 50u : static_cast<std::size_t>(std::stoul(maxStr));
            cb(jsonResponse(api.getRecentEvents(maxEvents)));
        },
        {Get});

    app().registerHandler(
        "/api/diag/log/export",
        [&api, &requireRole](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Viewer))
                return;
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
        "/api/diag/log/export",
        [&api, &requireRole, &sanitizePath, jsonResponse](const HttpRequestPtr& req,
                                                          std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Developer))
                return;
            auto json = req->getJsonObject();
            if (!json || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "path required"}}, k400BadRequest));
                return;
            }
            auto path = sanitizePath((*json)["path"].asString());
            if (!path)
            {
                cb(jsonResponse({{"error", "invalid path"}}, k400BadRequest));
                return;
            }
            auto maxEvents = static_cast<std::size_t>(json->get("max", 200).asUInt());
            auto format    = json->get("format", "text").asString();
            auto asJson    = format == "json";
            if (!api.exportRecentEventsToFile(maxEvents, asJson, *path))
            {
                cb(jsonResponse({{"error", "export failed"}}, k500InternalServerError));
                return;
            }
            cb(jsonResponse({{"exported", path->string()}, {"format", asJson ? "json" : "text"}}));
        },
        {Post});

    app().registerHandler(
        "/api/diag/pcap/export",
        [&api, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Viewer))
                return;
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
        "/api/diag/pcap/export",
        [&api, &requireRole, &sanitizePath, jsonResponse](const HttpRequestPtr& req,
                                                          std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Developer))
                return;
            auto json = req->getJsonObject();
            if (!json || !json->isMember("path"))
            {
                cb(jsonResponse({{"error", "path required"}}, k400BadRequest));
                return;
            }
            auto path = sanitizePath((*json)["path"].asString());
            if (!path)
            {
                cb(jsonResponse({{"error", "invalid path"}}, k400BadRequest));
                return;
            }
            if (!api.exportPcapCapture(*path))
            {
                cb(jsonResponse({{"error", "pcap export failed"}}, k500InternalServerError));
                return;
            }
            cb(jsonResponse({{"exported", path->string()}, {"format", "pcap"}}));
        },
        {Post});

    app().registerHandler(
        "/api/diag/log/file",
        [&api, &requireRole, jsonResponse](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Viewer))
                return;
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
                          [&api, &requireRole, jsonResponse](const HttpRequestPtr& req,
                                                             std::function<void(const HttpResponsePtr&)>&& cb)
                          {
                              if (!requireRole(req, cb, auth::Role::Viewer))
                                  return;
                              cb(jsonResponse(api.getDiagnosticsMetrics()));
                          },
                          {Get});

    app().registerHandler(
        "/api/diag/event",
        [&api, &requireRole, &sanitizeBoundedText, jsonResponse](const HttpRequestPtr& req,
                                                                 std::function<void(const HttpResponsePtr&)>&& cb)
        {
            if (!requireRole(req, cb, auth::Role::Developer))
                return;
            auto json = req->getJsonObject();
            if (!json || !json->isMember("component") || !json->isMember("message"))
            {
                cb(jsonResponse({{"error", "component and message required"}}, k400BadRequest));
                return;
            }
            auto severityStr = sanitizeBoundedText(json->get("severity", "INFO").asString(), 32);
            auto component   = sanitizeBoundedText((*json)["component"].asString(), 64);
            auto message     = sanitizeBoundedText((*json)["message"].asString(), 512);
            api.triggerDiagnosticEvent(severityStr, component, message);
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
