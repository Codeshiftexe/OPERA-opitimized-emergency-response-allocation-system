#pragma once

// Captcha checked by the server.
//
// Before: the sign-in page picked a code from captcha.txt and compared the
// answer itself, so a script calling /api/login directly skipped it entirely.
//
// Now: the server picks a random code (SecureRandom), draws it as a small
// image, remembers the answer under a random id, and /api/login refuses to
// check a password unless the id and the right answer come with it.
//   - single use: an id is spent on its first check, right or wrong, so a
//     solved captcha cannot be replayed for many password guesses;
//   - expires after kLifetimeSeconds;
//   - at most kMaxPending unanswered captchas are kept (memory stays bounded).
// The code is only ever sent as an image, never as text, so a script cannot
// simply read it from the reply.

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace opera {

struct CaptchaChallenge {
    std::string id;     // 32 hex characters
    std::string image;  // "data:image/bmp;base64,..." for an <img src>
};

class Captcha {
public:
    static constexpr int kLength = 6;
    static constexpr int kLifetimeSeconds = 120;
    static constexpr std::size_t kMaxPending = 2000;

    CaptchaChallenge issue();

    // True only for a known, unexpired id with the right answer (case and
    // spaces ignored). The id is used up either way.
    bool check(const std::string& id, const std::string& answer);

    std::size_t pending() const { return pending_.size(); }

    // For tests: the code behind `id` ("" if unknown), and a shorter lifetime.
    std::string answerForTest(const std::string& id) const;
    void setLifetimeForTest(int seconds) { lifetimeSeconds_ = seconds; }

private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::string code;
        Clock::time_point expires;
    };
    void purge(Clock::time_point now);

    std::unordered_map<std::string, Entry> pending_;
    int lifetimeSeconds_ = kLifetimeSeconds;
};

// The code drawn as a distorted 180x60 image, as a data URL (exposed for tests).
std::string captchaImage(const std::string& code);

}  // namespace opera
