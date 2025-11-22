#include "realtime_hub.hpp"

#include <drogon/HttpAppFramework.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace realtime
{

    RealtimeHub::RealtimeHub(trdp_sim::EngineContext& ctx, api::BackendApi& api, diag::DiagnosticManager& diag,
                             auth::AuthManager& auth)
        : m_ctx(ctx), m_api(api), m_diag(diag), m_auth(auth)
    {
    }

    void RealtimeHub::start()
    {
        // aim for ~10 Hz broadcast cadence
        m_timer = drogon::app().getLoop()->runEvery(0.1, [this]() { broadcast(); });
    }

    void RealtimeHub::stop()
    {
        if (m_timer)
            drogon::app().getLoop()->invalidateTimer(m_timer);
        m_timer = drogon::TimerId();
    }

    void RealtimeHub::registerConnection(const drogon::WebSocketConnectionPtr& conn, const std::string& token)
    {
        auto session = m_auth.validate(token);
        if (!session)
            return;
        const auto key = connectionKey(conn);
        std::lock_guard<std::mutex> lk(m_mtx);
        m_connections[key] = *session;
        m_connRefs[key]    = conn;
    }

    void RealtimeHub::unregisterConnection(const drogon::WebSocketConnectionPtr& conn)
    {
        const auto key = connectionKey(conn);
        std::lock_guard<std::mutex> lk(m_mtx);
        m_connections.erase(key);
        m_connRefs.erase(key);
    }

    void RealtimeHub::handleClientMessage(const drogon::WebSocketConnectionPtr& conn, const std::string& msg)
    {
        // Client messages can be used for pings or requesting theme changes; keep simple for now
        if (msg.empty())
            return;
        const auto key = connectionKey(conn);
        std::lock_guard<std::mutex> lk(m_mtx);
        auto                       it = m_connections.find(key);
        if (it == m_connections.end())
            return;
        // allow theme update
        if (msg == "theme:dark")
            it->second.theme = "dark";
        else if (msg == "theme:light")
            it->second.theme = "light";
    }

    void RealtimeHub::broadcast()
    {
        std::unordered_map<std::string, auth::Session> sessions;
        std::vector<drogon::WebSocketConnectionPtr>    conns;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            sessions = m_connections;
            conns.reserve(m_connRefs.size());
            for (const auto& [key, weakConn] : m_connRefs)
            {
                auto locked = weakConn.lock();
                if (locked)
                    conns.push_back(locked);
            }
        }

        if (conns.empty())
            return;

        nlohmann::json payload;
        payload["pd"]       = m_api.getPdStatus();
        payload["metrics"]  = m_api.getDiagnosticsMetrics();
        payload["datasets"] = nlohmann::json::array();
        for (const auto& [id, inst] : m_ctx.dataSetInstances)
        {
            if (!inst || !inst->def)
                continue;
            nlohmann::json summary;
            summary["dataSetId"] = id;
            summary["name"]      = inst->def->name;
            summary["locked"]    = inst->locked;
            summary["size"]      = inst->values.size();
            payload["datasets"].push_back(summary);
        }

        auto events = m_diag.fetchRecent(10);
        for (const auto& ev : events)
        {
            nlohmann::json e;
            e["component"] = ev.component;
            e["message"]   = ev.message;
            e["severity"]  = static_cast<int>(ev.severity);
            payload["events"].push_back(e);
        }

        const auto serialized = payload.dump();
        for (const auto& conn : conns)
        {
            if (conn && conn->connected())
                conn->send(serialized);
        }
    }

    std::string RealtimeHub::connectionKey(const drogon::WebSocketConnectionPtr& conn) const
    {
        std::ostringstream oss;
        oss << static_cast<const void*>(conn.get());
        return oss.str();
    }

} // namespace realtime

