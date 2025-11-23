#include "realtime_stream.hpp"

#include <drogon/drogon.h>

auth::AuthManager*     RealtimeStream::s_auth = nullptr;
realtime::RealtimeHub* RealtimeStream::s_hub  = nullptr;

void RealtimeStream::bootstrap(auth::AuthManager* auth, realtime::RealtimeHub* hub)
{
    s_auth = auth;
    s_hub  = hub;
}

void RealtimeStream::handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& conn)
{
    if (!s_auth || !s_hub)
    {
        conn->shutdown();
        return;
    }

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

    if (!s_auth->validate(token))
    {
        conn->send("unauthorized");
        conn->shutdown();
        return;
    }

    s_hub->registerConnection(conn, token);
}

void RealtimeStream::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn)
{
    if (s_hub)
        s_hub->unregisterConnection(conn);
}

void RealtimeStream::handleMessage(const drogon::WebSocketConnectionPtr& conn, std::string&& message,
                                   const drogon::WebSocketMessageType& type)
{
    handleMessageImpl(conn, message, type);
}

void RealtimeStream::handleMessage(const drogon::WebSocketConnectionPtr& conn, const std::string& message,
                                   const drogon::WebSocketMessageType& type)
{
    handleMessageImpl(conn, message, type);
}

void RealtimeStream::handleMessageImpl(const drogon::WebSocketConnectionPtr& conn, const std::string& message,
                                       const drogon::WebSocketMessageType& type)
{
    if (type == drogon::WebSocketMessageType::Text && s_hub)
        s_hub->handleClientMessage(conn, message);
}

