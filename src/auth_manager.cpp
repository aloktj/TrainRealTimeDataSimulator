#include "auth_manager.hpp"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>
#include <string_view>

#include <openssl/evp.h>
#include <openssl/rand.h>

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
        const auto ttlEnv = getEnvOrDefault("TRDP_SESSION_TTL_MIN", "30");
        try
        {
            auto ttl = std::stoul(ttlEnv);
            if (ttl > 0 && ttl <= 24 * 60)
                m_sessionTtl = std::chrono::minutes(ttl);
        }
        catch (const std::exception&)
        {
        }
    }

    std::optional<Session> AuthManager::login(const std::string& username, const std::string& password)
    {
        pruneExpired();
        auto it = m_users.find(username);
        if (it == m_users.end())
            return std::nullopt;
        if (!verifyPassword(password, it->second))
            return std::nullopt;

        Session sess;
        sess.token     = generateToken();
        sess.csrfToken = generateCsrfToken();
        sess.username  = username;
        sess.role      = it->second.role;
        sess.expiresAt = std::chrono::system_clock::now() + m_sessionTtl;
        sess.lastAccess = std::chrono::system_clock::now();
        m_sessions[sess.token] = sess;
        return sess;
    }

    std::optional<Session> AuthManager::validate(const std::string& token)
    {
        pruneExpired();
        auto it = m_sessions.find(token);
        if (it == m_sessions.end())
            return std::nullopt;
        refreshSession(it->second);
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

    std::string AuthManager::generateCsrfToken() const
    {
        std::array<unsigned char, 32> bytes{};
        if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        {
            std::random_device rd;
            for (auto& b : bytes)
                b = static_cast<unsigned char>(rd());
        }
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto b : bytes)
            oss << std::setw(2) << static_cast<int>(b);
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
        addOrUpdateUser("admin", getEnvOrDefault("TRDP_ADMIN_PASSWORD", "admin123"), Role::Admin);
        addOrUpdateUser("developer", getEnvOrDefault("TRDP_DEV_PASSWORD", "dev123"), Role::Developer);
        addOrUpdateUser("viewer", getEnvOrDefault("TRDP_VIEWER_PASSWORD", "viewer123"), Role::Viewer);
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

    void AuthManager::refreshSession(Session& session)
    {
        const auto now = std::chrono::system_clock::now();
        if (session.expiresAt <= now)
            return;
        session.lastAccess = now;
        session.expiresAt  = now + m_sessionTtl;
    }

    std::string AuthManager::generateSalt() const
    {
        std::array<unsigned char, 16> saltBytes{};
        if (RAND_bytes(saltBytes.data(), static_cast<int>(saltBytes.size())) != 1)
        {
            std::random_device rd;
            for (auto& b : saltBytes)
                b = static_cast<unsigned char>(rd());
        }

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto b : saltBytes)
            oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

    std::string AuthManager::hashPassword(const std::string& password, const std::string& salt) const
    {
        constexpr unsigned int iterations   = 120000;
        constexpr std::size_t  derivedBytes = 32;
        std::vector<unsigned char> output(derivedBytes);

        const int rc = PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                                         reinterpret_cast<const unsigned char*>(salt.data()),
                                         static_cast<int>(salt.size()), iterations, EVP_sha256(),
                                         static_cast<int>(output.size()), output.data());
        if (rc != 1)
            return {};

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto b : output)
            oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

    bool AuthManager::verifyPassword(const std::string& password, const UserRecord& record) const
    {
        if (record.salt.empty() || record.passwordHash.empty())
            return false;
        return hashPassword(password, record.salt) == record.passwordHash;
    }

    void AuthManager::addOrUpdateUser(const std::string& username, const std::string& password, Role role)
    {
        const auto salt = generateSalt();
        m_users[username] = UserRecord{hashPassword(password, salt), salt, role};
    }

    bool AuthManager::isPasswordHashOpaque(const std::string& username, const std::string& plain) const
    {
        auto it = m_users.find(username);
        if (it == m_users.end())
            return false;
        return it->second.passwordHash != plain && !it->second.passwordHash.empty();
    }

} // namespace auth

