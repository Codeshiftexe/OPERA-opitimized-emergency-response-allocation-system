#include "Auth.hpp"

#include <cctype>
#include <fstream>
#include <random>
#include <string>

#include "CityLoader.hpp"  // LoadError
#include "Sha256.hpp"

namespace opera {

namespace {

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

Auth::LoginResult Auth::login(const std::string& name, const std::string& password,
                              std::string& token, User& user, int& secondsLeft) {
    using Clock = std::chrono::steady_clock;
    const std::string key = lower(trim(name));
    Failures& f = failures_[key];
    const auto now = Clock::now();
    if (now < f.lockedUntil) {
        secondsLeft = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(f.lockedUntil - now).count()) + 1;
        return LoginResult::LockedOut;
    }

    const Account* match = nullptr;
    for (const Account& a : accounts_) {
        if (lower(a.name) == key) match = &a;
    }
    // Hash even for an unknown name, so both cases take the same time.
    const std::string hash = sha256Hex((match ? match->salt : std::string("no-such-user")) + password);
    if (match == nullptr || !sameHash(hash, match->hash)) {
        if (++f.count >= kMaxFails) {
            f.count = 0;
            const int lockSeconds = kLockSeconds;  // copy: works before C++17 too
            f.lockedUntil = now + std::chrono::seconds(lockSeconds);
        }
        return LoginResult::BadCredentials;
    }

    failures_.erase(key);
    user = User{match->name, match->role};
    token = newToken();
    sessions_[token] = user;
    return LoginResult::Ok;
}

const User* Auth::find(const std::string& token) const {
    const auto it = sessions_.find(token);
    return it == sessions_.end() ? nullptr : &it->second;
}

bool Auth::logout(const std::string& token) { return sessions_.erase(token) != 0; }

std::string Auth::newToken() {
    // Some older MinGW builds return the same std::random_device sequence on
    // every run, so mix in the clock and a counter, then hash it all.
    std::random_device rd;
    std::string seed;
    for (int i = 0; i < 8; ++i) seed += std::to_string(rd()) + ':';
    seed += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ':';
    seed += std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ':';
    seed += std::to_string(++tokenCounter_);
    return sha256Hex(seed);
}

}  // namespace opera
