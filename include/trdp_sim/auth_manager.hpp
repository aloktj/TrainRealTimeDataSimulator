#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace auth
{
    enum class Role
    {
        Admin,
        Developer,
        Viewer
    };

    struct Session
    {
        std::string                           token;
        std::string                           username;
        Role                                  role{Role::Viewer};
        std::chrono::system_clock::time_point expiresAt{};
        std::string                           theme{"light"};
    };

    class AuthManager
    {
      public:
        AuthManager();

        std::optional<Session> login(const std::string& username, const std::string& password);
        std::optional<Session> validate(const std::string& token);
        void                   logout(const std::string& token);
        bool                   updateTheme(const std::string& token, const std::string& theme);

      private:
        struct UserRecord
        {
            std::string password;
            Role        role{Role::Viewer};
        };

        std::unordered_map<std::string, UserRecord> m_users;
        std::unordered_map<std::string, Session>    m_sessions;

        std::string generateToken() const;
        Role        parseRole(const std::string& roleStr) const;
        void        loadDefaultsFromEnv();
        void        pruneExpired();
    };

    inline std::string roleToString(Role role)
    {
        switch (role)
        {
        case Role::Admin:
            return "Admin";
        case Role::Developer:
            return "Developer";
        case Role::Viewer:
            return "Viewer";
        }
        return "Viewer";
    }

} // namespace auth

