#pragma once

#include <drogon/WebSocketConnection.h>
#include <trantor/net/EventLoop.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "auth_manager.hpp"
#include "backend_api.hpp"

namespace realtime
{
    class RealtimeHub
    {
      public:
        RealtimeHub(trdp_sim::EngineContext& ctx, api::BackendApi& api, diag::DiagnosticManager& diag,
                    auth::AuthManager& auth);
        void start();
        void stop();

        void registerConnection(const drogon::WebSocketConnectionPtr& conn, const std::string& token);
        void unregisterConnection(const drogon::WebSocketConnectionPtr& conn);
        void handleClientMessage(const drogon::WebSocketConnectionPtr& conn, const std::string& msg);

      private:
        void broadcast();
        std::string connectionKey(const drogon::WebSocketConnectionPtr& conn) const;

        trdp_sim::EngineContext& m_ctx;
        api::BackendApi&         m_api;
        diag::DiagnosticManager& m_diag;
        auth::AuthManager&       m_auth;

        std::mutex                                             m_mtx;
        std::unordered_map<std::string, auth::Session>         m_connections;
        std::unordered_map<std::string, std::weak_ptr<drogon::WebSocketConnection>> m_connRefs;
        trantor::TimerId                                       m_timer{};
    };

} // namespace realtime

