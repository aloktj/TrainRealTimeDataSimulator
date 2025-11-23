#pragma once

#include <drogon/WebSocketController.h>

#include "auth_manager.hpp"
#include "realtime_hub.hpp"

class RealtimeStream : public drogon::WebSocketController<RealtimeStream>
{
  public:
    RealtimeStream();
    ~RealtimeStream() override = default;

    static void bootstrap(auth::AuthManager* auth, realtime::RealtimeHub* hub);

    void handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& conn) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;

    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/ws/realtime", drogon::Get, drogon::Options);
    WS_PATH_LIST_END

  private:
    void handleMessageImpl(const drogon::WebSocketConnectionPtr& conn, const std::string& message,
                           const drogon::WebSocketMessageType& type);

    static auth::AuthManager*     s_auth;
    static realtime::RealtimeHub* s_hub;
};

