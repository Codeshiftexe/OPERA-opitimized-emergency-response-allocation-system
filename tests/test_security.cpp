// Security tests: each one tries an attack and checks that it now fails.
//
//   Fix 1  captcha checked by the server (skip it, replay it, reuse a wrong one)
//   Fix 3  session tokens from the system's secure random generator
//   Fix 4  sessions expire (idle time and maximum age)
//   Fix 5  failed sign-ins limited per network address, across all names
//   Fix 7  requests with Transfer-Encoding or conflicting lengths refused
//   Fix 8  only plain ASCII names (no look-alike Unicode)
//   Fix 9  failure, session and captcha tables stay bounded
//
// Build: g++ -std=c++17 -Iinclude tests/test_security.cpp src/Api.cpp src/Auth.cpp
//        src/AutoDispatcher.cpp src/BatchPlanner.cpp src/Captcha.cpp src/CityLoader.cpp
//        src/DispatchCoordinator.cpp src/Emergency.cpp src/Graph.cpp src/Hospital.cpp
//        src/HttpParse.cpp src/Json.cpp src/LiveTracker.cpp src/Registry.cpp
//        src/RoutingEngine.cpp src/SecureRandom.cpp src/Sha256.cpp src/StateJson.cpp
//        src/TriageQueue.cpp src/Ambulance.cpp (Windows: add -ladvapi32)

#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Api.hpp"
#include "HttpServer.hpp"
#include "SecureRandom.hpp"
#include "test_util.hpp"

using namespace opera;

namespace {

enum Node { A, B, C, D };

// Accounts: boss (admin, password pw-admin) and op (operator, password pw-oper).
const char* kUsers =
    "USER boss | admin | s1 | d534bf292ac365a54fc4c70a6048c9835e90f0845c7819ed19f3d34d36d46ab0\n"
    "USER op | operator | s2 | c759c858afb12c7b927596b80c328e34a6e07ab67d22f17f24a55ae2c5c8af5f\n";

Auth makeAuth() {
    std::istringstream in(kUsers);
    Auth a;
    a.loadUsers(in, "test-passwords");
    return a;
}

std::string field(const std::string& body, const std::string& key) {
    const std::string k = "\"" + key + "\":\"";
    const auto p = body.find(k);
    if (p == std::string::npos) return "";
    const auto e = body.find('"', p + k.size());
    return body.substr(p + k.size(), e - p - k.size());
}

bool isHex(const std::string& s, std::size_t len) {
    if (s.size() != len) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// No threads (older MinGW lacks them), so wait by watching the clock.
void waitMs(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) { }
}

struct World {
    DispatchCoordinator coord;
    Auth auth;
    Api api;   // captcha required: the server default
    World() : coord(makeRegistry()), auth(makeAuth()), api(coord, makeLayout(), auth) {}
    static Registry makeRegistry() {
        Graph g;
        for (const char* n : {"A", "B", "C", "D"}) g.addNode(n);
        g.addEdge(A, B, 4);
        g.addEdge(B, C, 3);
        g.addEdge(C, D, 5);
        Registry r(g);
        r.addHospital(Hospital(1, D, 2));
        r.addAmbulance(Ambulance(10, A));
        return r;
    }
    static MapLayout makeLayout() {
        MapLayout m;
        m.nodePositions.assign(4, Point{0, 0});
        m.hospitalNames = {{1, "D General"}};
        return m;
    }
    // A fresh captcha: returns its id and puts the right answer in `answer`.
    std::string newCaptcha(std::string& answer) {
        const std::string id = field(api.handle("GET", "/api/captcha", "").body, "captchaId");
        answer = api.captcha().answerForTest(id);
        return id;
    }
    ApiResponse login(const std::string& name, const std::string& pw, const std::string& id,
                      const std::string& answer, const std::string& remote = "") {
        return api.handle("POST", "/api/login",
                          "{\"name\":\"" + name + "\",\"password\":\"" + pw + "\",\"captchaId\":\"" + id +
                              "\",\"captcha\":\"" + answer + "\"}",
                          "", remote);
    }
};

// ---- Fix 1: captcha checked by the server ---------------------------------

void testLoginWithoutCaptchaRefused() {
    World w;
    // The old attack: call /api/login directly, as a script would, with no captcha at all.
    const ApiResponse r = w.api.handle("POST", "/api/login", R"({"name":"boss","password":"pw-admin"})");
    CHECK_EQ(r.status, 400);
    CHECK(field(r.body, "token").empty());
    CHECK_EQ(w.auth.sessionCount(), std::size_t(0));
}

void testCaptchaIsAnImageNotText() {
    World w;
    const ApiResponse r = w.api.handle("GET", "/api/captcha", "");
    CHECK_EQ(r.status, 200);
    const std::string id = field(r.body, "captchaId");
    CHECK(isHex(id, 32));
    const std::string image = field(r.body, "image");
    CHECK(image.compare(0, 22, "data:image/bmp;base64,") == 0);
    CHECK(image.size() > 40000);                  // a 180x60 picture, not a few characters
    const std::string answer = w.api.captcha().answerForTest(id);
    CHECK_EQ(answer.size(), std::size_t(Captcha::kLength));
    CHECK(r.body.find("\"" + answer + "\"") == std::string::npos);   // the code is never sent as text
    CHECK_EQ(w.api.handle("POST", "/api/captcha", "").status, 405);
}

void testRightCaptchaSignsIn() {
    World w;
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    std::string lowered;
    for (char c : answer) lowered += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    const ApiResponse r = w.login("boss", "pw-admin", id, " " + lowered + " ");  // case and spaces ignored
    CHECK_EQ(r.status, 200);
    CHECK(isHex(field(r.body, "token"), 64));
}

void testCaptchaCannotBeReplayed() {
    World w;
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    CHECK_EQ(w.login("boss", "pw-admin", id, answer).status, 200);
    // The same solved captcha again, e.g. to try many passwords: refused.
    CHECK_EQ(w.login("boss", "pw-admin", id, answer).status, 400);
    CHECK_EQ(w.login("op", "pw-oper", id, answer).status, 400);
}

void testWrongAnswerUsesUpTheCaptcha() {
    World w;
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    CHECK_EQ(w.login("boss", "pw-admin", id, "WRONG1").status, 400);
    // Guessing again on the same image is not possible: the id is gone.
    CHECK_EQ(w.login("boss", "pw-admin", id, answer).status, 400);
    CHECK_EQ(w.login("boss", "pw-admin", "", "").status, 400);
    CHECK_EQ(w.login("boss", "pw-admin", "made-up-id", "ABCDEF").status, 400);
}

void testCaptchaFailureIsCheckedBeforeThePassword() {
    World w;
    // Wrong captchas never reach the password check, so they do not lock the account...
    bool all400 = true;
    for (int i = 0; i < Auth::kMaxFails + 2; ++i) all400 = all400 && w.login("boss", "x", "bad", "bad").status == 400;
    CHECK(all400);
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    CHECK_EQ(w.login("boss", "pw-admin", id, answer).status, 200);
}

void testCaptchaExpires() {
    World w;
    w.api.captcha().setLifetimeForTest(1);
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    waitMs(1100);
    CHECK_EQ(w.login("boss", "pw-admin", id, answer).status, 400);
}

void testCaptchaImageIsAValidBitmap() {
    const std::string url = captchaImage("AB3XY7");
    const std::string b64 = url.substr(22);
    // Decode the first 30 base64 characters (22 bytes) and read the BMP header.
    const std::string t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> bytes;
    for (std::size_t i = 0; i + 4 <= 32; i += 4) {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) v = (v << 6) | static_cast<unsigned>(t.find(b64[i + k]));
        bytes.push_back(static_cast<unsigned char>(v >> 16));
        bytes.push_back(static_cast<unsigned char>(v >> 8));
        bytes.push_back(static_cast<unsigned char>(v));
    }
    CHECK(bytes[0] == 'B' && bytes[1] == 'M');
    const int width = bytes[18] | (bytes[19] << 8);
    const int height = bytes[22] | (bytes[23] << 8);
    CHECK_EQ(width, 180);
    CHECK_EQ(height, 60);
    CHECK(captchaImage("AB3XY7") != url);         // fresh noise every time
}

// ---- Fix 3: unpredictable tokens --------------------------------------------

void testSecureRandom() {
    std::set<std::string> seen;
    int digitCount[16] = {0};
    bool allHex = true;
    for (int i = 0; i < 1000; ++i) {
        const std::string h = secureRandomHex(32);
        allHex = allHex && isHex(h, 64);
        seen.insert(h);
        for (char c : h) ++digitCount[c <= '9' ? c - '0' : c - 'a' + 10];
    }
    CHECK(allHex);
    CHECK_EQ(seen.size(), std::size_t(1000));     // no repeats
    bool even = true;                              // 64000 digits: about 4000 of each
    for (int d : digitCount) even = even && d > 3400 && d < 4600;
    CHECK(even);
    bool all[6] = {false};
    for (int i = 0; i < 300; ++i) all[secureRandomBelow(6)] = true;
    CHECK(all[0] && all[1] && all[2] && all[3] && all[4] && all[5]);
}

void testSessionTokensAreUnique() {
    Auth auth = makeAuth();
    std::set<std::string> tokens;
    bool allOk = true;
    for (int i = 0; i < 200; ++i) {
        std::string token;
        User u;
        int left = 0;
        allOk = allOk && auth.login("boss", "pw-admin", token, u, left) == Auth::LoginResult::Ok && isHex(token, 64);
        tokens.insert(token);
    }
    CHECK(allOk);
    CHECK_EQ(tokens.size(), std::size_t(200));
}

// ---- Fix 4: sessions expire -------------------------------------------------

std::string signIn(Auth& auth) {
    std::string token;
    User u;
    int left = 0;
    auth.login("op", "pw-oper", token, u, left);
    return token;
}

void testIdleSessionExpires() {
    Auth auth = makeAuth();
    auth.setSessionLimits(1, 100);
    const std::string token = signIn(auth);
    CHECK(auth.find(token) != nullptr);
    waitMs(1200);
    CHECK(auth.find(token) == nullptr);           // a stolen token stops working
    CHECK_EQ(auth.sessionCount(), std::size_t(0));
}

void testUseKeepsSessionAlive() {
    Auth auth = makeAuth();
    auth.setSessionLimits(2, 100);
    const std::string token = signIn(auth);
    waitMs(1200);
    CHECK(auth.find(token) != nullptr);           // used: idle time starts again
    waitMs(1200);
    CHECK(auth.find(token) != nullptr);           // 2.4 s in total, but never 2 s unused
}

void testMaximumSessionAge() {
    Auth auth = makeAuth();
    auth.setSessionLimits(100, 1);
    const std::string token = signIn(auth);
    waitMs(600);
    CHECK(auth.find(token) != nullptr);
    waitMs(600);
    CHECK(auth.find(token) == nullptr);           // ends even though it was in use
}

void testExpiredTokenRejectedByApi() {
    World w;
    w.auth.setSessionLimits(1, 100);
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    const std::string token = field(w.login("boss", "pw-admin", id, answer).body, "token");
    CHECK_EQ(w.api.handle("GET", "/api/session", "", "Bearer " + token).status, 200);
    waitMs(1200);
    CHECK_EQ(w.api.handle("GET", "/api/session", "", "Bearer " + token).status, 401);
}

// ---- Fix 5: per-address limit -----------------------------------------------

Auth::LoginResult attempt(Auth& auth, const std::string& name, const std::string& pw,
                          const std::string& address) {
    std::string token;
    User u;
    int left = 0;
    return auth.login(name, pw, token, u, left, address);
}

void testAddressLockedAcrossNames() {
    Auth auth = makeAuth();
    // Guess across many names, fewer than 5 each, so no single name ever locks.
    bool allBad = true;
    for (int i = 0; i < Auth::kMaxAddressFails; ++i) {
        allBad = allBad && attempt(auth, "guess" + std::to_string(i), "x", "10.0.0.7") == Auth::LoginResult::BadCredentials;
    }
    CHECK(allBad);
    // Now even the right password is refused from that address...
    CHECK(attempt(auth, "boss", "pw-admin", "10.0.0.7") == Auth::LoginResult::LockedOut);
    // ...but other computers are not affected.
    CHECK(attempt(auth, "boss", "pw-admin", "10.0.0.8") == Auth::LoginResult::Ok);
}

void testSuccessDoesNotResetAddressCount() {
    Auth auth = makeAuth();
    for (int i = 0; i < Auth::kMaxAddressFails - 1; ++i) attempt(auth, "n" + std::to_string(i), "x", "10.0.0.9");
    CHECK(attempt(auth, "op", "pw-oper", "10.0.0.9") == Auth::LoginResult::Ok);   // a known password...
    attempt(auth, "another", "x", "10.0.0.9");                                    // ...does not wipe the count
    CHECK(attempt(auth, "boss", "pw-admin", "10.0.0.9") == Auth::LoginResult::LockedOut);
}

void testAddressLimitThroughApi() {
    World w;
    for (int i = 0; i < Auth::kMaxAddressFails; ++i) {
        std::string answer;
        const std::string id = w.newCaptcha(answer);
        w.login("user" + std::to_string(i), "x", id, answer, "192.168.1.50");
    }
    std::string answer;
    const std::string id = w.newCaptcha(answer);
    const ApiResponse r = w.login("boss", "pw-admin", id, answer, "192.168.1.50");
    CHECK_EQ(r.status, 429);
    CHECK(r.body.find("Try again in") != std::string::npos);
}

// ---- Fix 7: request smuggling defence ---------------------------------------

bool parse(const std::string& head, HttpRequest& req, std::size_t& len, HttpResponse& err) {
    return parseRequestHead(head, req, len, err);
}

void testNormalRequestParses() {
    HttpRequest req;
    std::size_t len = 0;
    HttpResponse err;
    CHECK(parse("POST /api/login?x=1 HTTP/1.1\r\nHost: localhost\r\nContent-Length: 12\r\n"
                "Authorization: Bearer abc\r\nOrigin: http://localhost:8080", req, len, err));
    CHECK_EQ(req.method, std::string("POST"));
    CHECK_EQ(req.path, std::string("/api/login"));
    CHECK_EQ(len, std::size_t(12));
    CHECK_EQ(req.authorization, std::string("Bearer abc"));
    HttpRequest same;
    CHECK(parse("POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 5", same, len, err));   // identical twice: fine
}

void testTransferEncodingRefused() {
    for (const char* head : {"POST / HTTP/1.1\r\nTransfer-Encoding: chunked",
                             "POST / HTTP/1.1\r\nContent-Length: 4\r\nTransfer-Encoding: chunked",
                             "POST / HTTP/1.1\r\nTRANSFER-ENCODING: chunked\r\nContent-Length: 4"}) {
        HttpRequest req;
        std::size_t len = 0;
        HttpResponse err;
        CHECK(!parse(head, req, len, err));
        CHECK_EQ(err.status, 501);
    }
}

void testBadContentLengthRefused() {
    for (const char* head : {"POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 50",
                             "POST / HTTP/1.1\r\nContent-Length: 5abc",
                             "POST / HTTP/1.1\r\nContent-Length: -1",
                             "POST / HTTP/1.1\r\nContent-Length: +5",
                             "POST / HTTP/1.1\r\nContent-Length: "}) {
        HttpRequest req;
        std::size_t len = 0;
        HttpResponse err;
        CHECK(!parse(head, req, len, err));
        CHECK_EQ(err.status, 400);
    }
    HttpRequest req;
    std::size_t len = 0;
    HttpResponse err;
    CHECK(!parse("POST / HTTP/1.1\r\nContent-Length: 70000", req, len, err));
    CHECK_EQ(err.status, 413);
}

// ---- Fix 8: plain names only --------------------------------------------------

void testNameRules() {
    CHECK(Auth::validName("admin"));
    CHECK(Auth::validName("operator.1_night-shift"));
    CHECK(!Auth::validName(""));
    CHECK(!Auth::validName(std::string(33, 'a')));
    CHECK(!Auth::validName("ad min"));
    CHECK(!Auth::validName("a|b"));
    CHECK(!Auth::validName("\xD0\xB0" "dmin"));    // Cyrillic "а" + "dmin": looks exactly like admin
}

void testLookAlikeNameCannotBeAdded() {
    std::istringstream in(std::string(kUsers) +
        "USER \xD0\xB0" "dmin | admin | s3 | d534bf292ac365a54fc4c70a6048c9835e90f0845c7819ed19f3d34d36d46ab0\n");
    Auth a;
    bool refused = false;
    std::string message;
    try { a.loadUsers(in, "passwords.txt"); } catch (const LoadError& e) { refused = true; message = e.what(); }
    CHECK(refused);
    CHECK(message.find("passwords.txt:3:") != std::string::npos);   // says which line
}

void testLookAlikeNameCannotSignIn() {
    Auth auth = makeAuth();
    CHECK(attempt(auth, "b\xD0\xBEss", "pw-admin", "") == Auth::LoginResult::BadCredentials);   // Cyrillic "о"
    CHECK(attempt(auth, "BOSS", "pw-admin", "") == Auth::LoginResult::Ok);                      // plain case still fine
}

// ---- Fix 9: bounded tables ----------------------------------------------------

void testFailureTableBounded() {
    Auth auth = makeAuth();
    for (std::size_t i = 0; i < Auth::kMaxTracked + 500; ++i) attempt(auth, "random" + std::to_string(i), "x", "");
    CHECK(auth.trackedFailures() <= Auth::kMaxTracked);
}

void testSessionTableBounded() {
    Auth auth = makeAuth();
    std::string first;
    for (std::size_t i = 0; i < Auth::kMaxSessions + 50; ++i) {
        const std::string t = signIn(auth);
        if (i == 0) first = t;
    }
    CHECK(auth.sessionCount() <= Auth::kMaxSessions);
    CHECK(auth.find(first) == nullptr);           // the least recently used made room
}

void testCaptchaTableBounded() {
    Captcha c;
    for (std::size_t i = 0; i < Captcha::kMaxPending + 50; ++i) c.issue();
    CHECK(c.pending() <= Captcha::kMaxPending);
}

}  // namespace

int main() {
    testLoginWithoutCaptchaRefused();
    testCaptchaIsAnImageNotText();
    testRightCaptchaSignsIn();
    testCaptchaCannotBeReplayed();
    testWrongAnswerUsesUpTheCaptcha();
    testCaptchaFailureIsCheckedBeforeThePassword();
    testCaptchaExpires();
    testCaptchaImageIsAValidBitmap();
    testSecureRandom();
    testSessionTokensAreUnique();
    testIdleSessionExpires();
    testUseKeepsSessionAlive();
    testMaximumSessionAge();
    testExpiredTokenRejectedByApi();
    testAddressLockedAcrossNames();
    testSuccessDoesNotResetAddressCount();
    testAddressLimitThroughApi();
    testNormalRequestParses();
    testTransferEncodingRefused();
    testBadContentLengthRefused();
    testNameRules();
    testLookAlikeNameCannotBeAdded();
    testLookAlikeNameCannotSignIn();
    testFailureTableBounded();
    testSessionTableBounded();
    testCaptchaTableBounded();
    return report("test_security");
}
