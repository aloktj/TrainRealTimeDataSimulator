#include "auth_manager.hpp"

#include <cstdlib>
#include <random>
#include <sstream>
#include <string_view>

using namespace std::chrono_literals;

namespace
{
    std::string getEnvOrDefault(const char* key, const std::string& fallback)
    {
        const char* val = std::getenv(key);
        if (val && *val)
            return val;
        return fallback;
    }

} // namespace

namespace auth
{

    AuthManager::AuthManager()
    {
        loadDefaultsFromEnv();
    }

    std::optional<Session> AuthManager::login(const std::string& username, const std::string& password)
    {
        pruneExpired();
        auto it = m_users.find(username);
        if (it == m_users.end())
            return std::nullopt;
        if (it->second.password != password)
            return std::nullopt;

        Session sess;
        sess.token     = generateToken();
        sess.username  = username;
        sess.role      = it->second.role;
        sess.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(8);
        m_sessions[sess.token] = sess;
        return sess;
    }

    std::optional<Session> AuthManager::validate(const std::string& token)
    {
        pruneExpired();
        auto it = m_sessions.find(token);
        if (it == m_sessions.end())
            return std::nullopt;
        return it->second;
    }

    void AuthManager::logout(const std::string& token)
    {
        m_sessions.erase(token);
    }

    bool AuthManager::updateTheme(const std::string& token, const std::string& theme)
    {
        auto it = m_sessions.find(token);
        if (it == m_sessions.end())
            return false;
        it->second.theme = theme;
        return true;
    }

    std::string AuthManager::generateToken() const
    {
        std::random_device                    rd;
        std::mt19937_64                       gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream                    oss;
        oss << std::hex << dist(gen) << dist(gen);
        return oss.str();
    }

    Role AuthManager::parseRole(const std::string& roleStr) const
    {
        std::string lowered = roleStr;
        for (auto& c : lowered)
            c = static_cast<char>(::tolower(c));
        if (lowered == "admin")
            return Role::Admin;
        if (lowered == "developer" || lowered == "dev")
            return Role::Developer;
        return Role::Viewer;
    }

    void AuthManager::loadDefaultsFromEnv()
    {
        m_users.clear();
        m_sessions.clear();
        m_users.emplace("admin", UserRecord{getEnvOrDefault("TRDP_ADMIN_PASSWORD", "admin123"), Role::Admin});
        m_users.emplace("developer", UserRecord{getEnvOrDefault("TRDP_DEV_PASSWORD", "dev123"), Role::Developer});
        m_users.emplace("viewer", UserRecord{getEnvOrDefault("TRDP_VIEWER_PASSWORD", "viewer123"), Role::Viewer});
    }

    void AuthManager::pruneExpired()
    {
        const auto now = std::chrono::system_clock::now();
        for (auto it = m_sessions.begin(); it != m_sessions.end();)
        {
            if (it->second.expiresAt <= now)
                it = m_sessions.erase(it);
            else
                ++it;
        }
    }

} // namespace auth

