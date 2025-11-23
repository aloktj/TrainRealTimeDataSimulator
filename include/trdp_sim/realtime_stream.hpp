#pragma once

#include <drogon/WebSocketController.h>

#include "auth_manager.hpp"
#include "realtime_hub.hpp"

class RealtimeStream : public drogon::WebSocketController<RealtimeStream>
{
  public:
    RealtimeStream() = default;

    static void bootstrap(auth::AuthManager* auth, realtime::RealtimeHub* hub);

    void handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& conn) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;

    // Drogon has changed the websocket message callback signature across versions. Provide
    // overloads for both rvalue and const reference message arguments so at least one matches
    // the interface in use without relying on specific version macros.
    void handleMessage(const drogon::WebSocketConnectionPtr& conn, std::string&& message,
                      const drogon::WebSocketMessageType& type);
    void handleMessage(const drogon::WebSocketConnectionPtr& conn, const std::string& message,
                      const drogon::WebSocketMessageType& type);

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/ws/realtime", drogon::Get, drogon::Options);
    WS_PATH_LIST_END

  private:
    void handleMessageImpl(const drogon::WebSocketConnectionPtr& conn, const std::string& message,
                           const drogon::WebSocketMessageType& type);

    static auth::AuthManager*     s_auth;
    static realtime::RealtimeHub* s_hub;
};

