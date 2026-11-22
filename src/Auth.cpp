#include "Auth.hpp"

#include <cctype>
#include <fstream>
#include <string>

#include "CityLoader.hpp"  // LoadError
#include "SecureRandom.hpp"
#include "Sha256.hpp"

namespace opera {

// Definitions for the constants (needed before C++17 when one is passed by reference).
constexpr int Auth::kMaxFails;
constexpr int Auth::kLockSeconds;
constexpr int Auth::kMaxAddressFails;
constexpr int Auth::kAddressLockSeconds;
constexpr int Auth::kIdleSeconds;
constexpr int Auth::kMaxSessionSeconds;
constexpr std::size_t Auth::kMaxTracked;
constexpr std::size_t Auth::kMaxSessions;
constexpr std::size_t Auth::kMaxNameLength;

namespace {

// A failure count is forgotten after this long without a new failure.
constexpr int kForgetFailuresSeconds = 15 * 60;

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isLowerHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (const char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// Compares every character even after a mismatch, so the time taken does not
// reveal how much of a hash matched.
bool sameHash(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned>(static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    }
    return diff == 0;
}

[[noreturn]] void fail(const std::string& source, int line, const std::string& message) {
    throw LoadError(source + ":" + std::to_string(line) + ": " + message);
}

}  // namespace

void Auth::loadUsers(std::istream& in, const std::string& source) {
    accounts_.clear();
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;

        const auto space = line.find_first_of(" \t");
        if (lower(line.substr(0, space)) != "user" || space == std::string::npos) {
            fail(source, lineNo, "expected: USER name | role | salt | hash");
        }
        std::vector<std::string> f;
        std::string rest = line.substr(space);
        std::string::size_type start = 0;
        while (true) {
            const auto bar = rest.find('|', start);
            f.push_back(trim(rest.substr(start, bar - start)));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        if (f.size() != 4) fail(source, lineNo, "expected: USER name | role | salt | hash");

        Account a{f[0], lower(f[1]), f[2], lower(f[3])};
        if (a.name.empty()) fail(source, lineNo, "name is empty");
        if (!validName(a.name)) {
            fail(source, lineNo, "name '" + a.name + "' may only use the letters A-Z and a-z, digits, "
                                 "'.', '_' and '-' (at most 32 characters)");
        }
        if (a.role != "admin" && a.role != "operator") {
            fail(source, lineNo, "role must be admin or operator, not '" + f[1] + "'");
        }
        if (a.salt.empty()) fail(source, lineNo, "salt is empty");
        if (!isLowerHex64(a.hash)) fail(source, lineNo, "hash must be 64 hex characters (SHA-256)");
        for (const Account& other : accounts_) {
            if (lower(other.name) == lower(a.name)) {
                fail(source, lineNo, "the name '" + a.name + "' is used twice");
            }
        }
        accounts_.push_back(a);
    }
    if (accounts_.empty()) throw LoadError(source + ": no USER lines found, so nobody could sign in");
}

void Auth::loadUsersFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw LoadError(path + ": cannot open the file");
    loadUsers(in, path);
}

bool Auth::validName(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameLength) return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        if (!ok) return false;   // also rejects every non-ASCII byte, so no look-alike letters
    }
    return true;
}

Auth::LoginResult Auth::login(const std::string& name, const std::string& password,
                              std::string& token, User& user, int& secondsLeft,
                              const std::string& address) {
    const auto now = Clock::now();
    const std::string key = lower(trim(name));
    auto secondsUntil = [now](Clock::time_point t) {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(t - now).count()) + 1;
    };

    // A locked address may not try any name; a locked name may not be tried from anywhere.
    if (!address.empty()) {
        const auto a = addressFailures_.find(address);
        if (a != addressFailures_.end() && now < a->second.lockedUntil) {
            secondsLeft = secondsUntil(a->second.lockedUntil);
            return LoginResult::LockedOut;
        }
    }
    const auto f = failures_.find(key);
    if (f != failures_.end() && now < f->second.lockedUntil) {
        secondsLeft = secondsUntil(f->second.lockedUntil);
        return LoginResult::LockedOut;
    }

    const Account* match = nullptr;
    if (validName(trim(name))) {
        for (const Account& a : accounts_) {
            if (lower(a.name) == key) match = &a;
        }
    }
    // Hash even for an unknown name, so both cases take the same time.
    const std::string hash = sha256Hex((match ? match->salt : std::string("no-such-user")) + password);
    if (match == nullptr || !sameHash(hash, match->hash)) {
        addFailure(failures_, key, kMaxFails, kLockSeconds, now);
        if (!address.empty()) {
            addFailure(addressFailures_, address, kMaxAddressFails, kAddressLockSeconds, now);
        }
        return LoginResult::BadCredentials;
    }

    // Success clears the name's failures but not the address's: otherwise one
    // known password could be used to reset the count between guesses at others.
    failures_.erase(key);
    pruneSessions(now);
    user = User{match->name, match->role};
    token = newToken();
    sessions_[token] = Session{user, now, now};
    return LoginResult::Ok;
}

const User* Auth::find(const std::string& token) {
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) return nullptr;
    const auto now = Clock::now();
    if (expired(it->second, now)) {
        sessions_.erase(it);
        return nullptr;
    }
    it->second.lastUsed = now;
    return &it->second.user;
}

bool Auth::logout(const std::string& token) { return sessions_.erase(token) != 0; }

bool Auth::expired(const Session& s, Clock::time_point now) const {
    return now - s.lastUsed >= std::chrono::seconds(idleSeconds_) ||
           now - s.created >= std::chrono::seconds(maxSessionSeconds_);
}

void Auth::pruneSessions(Clock::time_point now) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (expired(it->second, now)) it = sessions_.erase(it);
        else ++it;
    }
    while (sessions_.size() >= kMaxSessions) {      // still full: end the least recently used
        auto oldest = sessions_.begin();
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        }
        sessions_.erase(oldest);
    }
}

void Auth::addFailure(std::unordered_map<std::string, Failures>& table, const std::string& key,
                      int maxFails, int lockSeconds, Clock::time_point now) {
    if (table.size() >= kMaxTracked && table.find(key) == table.end()) pruneFailures(table, now);
    Failures& e = table[key];
    if (e.count > 0 && now - e.lastFail >= std::chrono::seconds(kForgetFailuresSeconds)) e.count = 0;
    e.lastFail = now;
    if (++e.count >= maxFails) {
        e.count = 0;
        e.lockedUntil = now + std::chrono::seconds(lockSeconds);
    }
}

void Auth::pruneFailures(std::unordered_map<std::string, Failures>& table, Clock::time_point now) {
    // First forget entries that are neither locked nor recent.
    for (auto it = table.begin(); it != table.end();) {
        const bool locked = now < it->second.lockedUntil;
        const bool recent = now - it->second.lastFail < std::chrono::seconds(kForgetFailuresSeconds);
        if (!locked && !recent) it = table.erase(it);
        else ++it;
    }
    // Then any entry that is not locked, and finally anything, so memory stays bounded.
    for (auto it = table.begin(); it != table.end() && table.size() >= kMaxTracked;) {
        if (now >= it->second.lockedUntil) it = table.erase(it);
        else ++it;
    }
    while (table.size() >= kMaxTracked) table.erase(table.begin());
}

std::string Auth::newToken() {
    // 32 bytes from the operating system's secure generator (not std::random_device,
    // which repeats itself on some MinGW versions): 64 hex characters, unguessable.
    return secureRandomHex(32);
}

}  // namespace opera
