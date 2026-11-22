#pragma once

// Sign-in for the web interface, checked on the server.
//
// Accounts come from passwords.txt (same format as the original UI):
//     USER <name> | <admin|operator> | <salt> | <sha256(salt + password) in hex>
// Passwords are never stored, only salted hashes. The file lives in
// data/, which the server does NOT serve, so browsers cannot read it.
//
// A successful login creates a session token (64 hex characters from the
// operating system's secure random generator, see SecureRandom.hpp). The page
// sends it with every request as "Authorization: Bearer <token>". Sessions live
// in memory, so restarting the server signs everyone out.
//
// Security rules (see SECURITY.md):
//   - a session ends after kIdleSeconds without use, and after kMaxSessionSeconds
//     in any case, so a stolen token stops working;
//   - 5 failed attempts for a name lock that name for 30 seconds;
//   - 20 failed attempts from one address (any names) lock that address for
//     5 minutes, so guessing across many names is slowed too;
//   - names are plain ASCII letters, digits and . _ - (no look-alike Unicode);
//   - the failure tables and the session table have a size limit.

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

    static constexpr int kMaxFails = 5;                 // per name
    static constexpr int kLockSeconds = 30;
    static constexpr int kMaxAddressFails = 20;         // per address, any names
    static constexpr int kAddressLockSeconds = 300;
    static constexpr int kIdleSeconds = 30 * 60;        // session unused this long: ended
    static constexpr int kMaxSessionSeconds = 12 * 60 * 60;   // and never longer than this
    static constexpr std::size_t kMaxTracked = 10000;   // entries per failure table
    static constexpr std::size_t kMaxSessions = 10000;
    static constexpr std::size_t kMaxNameLength = 32;

    // Plain ASCII letters, digits, '.', '_' and '-', 1 to kMaxNameLength long.
    static bool validName(const std::string& name);

    // Reads passwords.txt. Throws LoadError ("<source>:<line>: ...") on a bad
    // line, a repeated name, or a file with no accounts.
    void loadUsers(std::istream& in, const std::string& source);
    void loadUsersFile(const std::string& path);

    // On Ok: `token` is a new session and `user` the account.
    // On LockedOut: `secondsLeft` says how long the lock lasts.
    // `address` is the caller's network address ("" when unknown, e.g. in tests).
    LoginResult login(const std::string& name, const std::string& password, std::string& token,
                      User& user, int& secondsLeft, const std::string& address = "");

    // nullptr if the token is unknown, signed out, expired or from before a
    // restart. A valid session counts as used, which restarts its idle time.
    const User* find(const std::string& token);

    bool logout(const std::string& token);

    std::size_t userCount() const { return accounts_.size(); }
    std::size_t sessionCount() const { return sessions_.size(); }
    std::size_t trackedFailures() const { return failures_.size() + addressFailures_.size(); }

    // Shorter limits for tests (or a stricter site): seconds of idle time and maximum age.
    void setSessionLimits(int idleSeconds, int maxSeconds) {
        idleSeconds_ = idleSeconds;
        maxSessionSeconds_ = maxSeconds;
    }

private:
    struct Account {
        std::string name;
        std::string role;
        std::string salt;
        std::string hash;
    };
    using Clock = std::chrono::steady_clock;
    struct Failures {
        int count = 0;
        Clock::time_point lockedUntil{};
        Clock::time_point lastFail{};
    };
    struct Session {
        User user;
        Clock::time_point created;
        Clock::time_point lastUsed;
    };

    std::string newToken();
    bool expired(const Session& s, Clock::time_point now) const;
    void pruneSessions(Clock::time_point now);
    // Counts one failure for `key`; the maxFails-th failure starts a lock of `lockSeconds`.
    static void addFailure(std::unordered_map<std::string, Failures>& table, const std::string& key,
                           int maxFails, int lockSeconds, Clock::time_point now);
    static void pruneFailures(std::unordered_map<std::string, Failures>& table, Clock::time_point now);

    std::vector<Account> accounts_;
    std::unordered_map<std::string, Session> sessions_;           // token -> session
    std::unordered_map<std::string, Failures> failures_;          // lower-case name -> failures
    std::unordered_map<std::string, Failures> addressFailures_;   // address -> failures
    int idleSeconds_ = kIdleSeconds;
    int maxSessionSeconds_ = kMaxSessionSeconds;
};

}  // namespace opera
