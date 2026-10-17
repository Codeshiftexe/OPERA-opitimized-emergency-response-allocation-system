#pragma once

// Sign-in for the web interface, checked on the server.
//
// Accounts come from passwords.txt (same format as the original UI):
//     USER <name> | <admin|operator> | <salt> | <sha256(salt + password) in hex>
// Passwords are never stored, only salted hashes. The file lives in
// data/, which the server does NOT serve, so browsers cannot read it.
//
// A successful login creates a session token (64 hex characters). The page
// sends it with every request as "Authorization: Bearer <token>". Sessions live
// in memory, so restarting the server signs everyone out.
//
// After 5 failed attempts for a name, that name is locked for 30 seconds.

#include <chrono>
#include <istream>
#include <string>
#include <unordered_map>
#include <vector>

namespace opera {

struct User {
    std::string name;
    std::string role;  // "admin" or "operator"
};

class Auth {
public:
    enum class LoginResult { Ok, BadCredentials, LockedOut };

    static constexpr int kMaxFails = 5;
    static constexpr int kLockSeconds = 30;

    // Reads passwords.txt. Throws LoadError ("<source>:<line>: ...") on a bad
    // line, a repeated name, or a file with no accounts.
    void loadUsers(std::istream& in, const std::string& source);
    void loadUsersFile(const std::string& path);

    // On Ok: `token` is a new session and `user` the account.
    // On LockedOut: `secondsLeft` says how long the lock lasts.
    LoginResult login(const std::string& name, const std::string& password, std::string& token,
                      User& user, int& secondsLeft);

    // nullptr if the token is unknown (never issued, signed out, or server restarted).
    const User* find(const std::string& token) const;

    bool logout(const std::string& token);

    std::size_t userCount() const { return accounts_.size(); }

private:
    struct Account {
        std::string name;
        std::string role;
        std::string salt;
        std::string hash;
    };
    struct Failures {
        int count = 0;
        std::chrono::steady_clock::time_point lockedUntil{};
    };

    std::string newToken();

    std::vector<Account> accounts_;
    std::unordered_map<std::string, User> sessions_;       // token -> user
    std::unordered_map<std::string, Failures> failures_;   // lower-case name -> failures
    unsigned long long tokenCounter_ = 0;
};

}  // namespace opera
