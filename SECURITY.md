# OPERA security

This page lists what protects the sign-in and the server, where each protection
lives in the code, which test attacks it, and what is still a known limit.

Run the security tests:

```
g++ -std=c++17 -Iinclude tests/test_security.cpp src/Api.cpp src/Auth.cpp src/AutoDispatcher.cpp src/BatchPlanner.cpp src/Captcha.cpp src/CityLoader.cpp src/DispatchCoordinator.cpp src/Emergency.cpp src/Graph.cpp src/Hospital.cpp src/HttpParse.cpp src/Json.cpp src/LiveTracker.cpp src/Registry.cpp src/RoutingEngine.cpp src/SecureRandom.cpp src/Sha256.cpp src/StateJson.cpp src/TriageQueue.cpp src/Ambulance.cpp -o test_security
./test_security
```

(On Windows add `-ladvapi32`.) Each test tries an attack and checks that it fails.
Removing any one of the protections below makes at least one of its checks fail.

## Protections

| Protection | What it stops | Where | Tested by |
|---|---|---|---|
| **Captcha made and checked by the server** | Scripts calling `/api/login` directly (the old captcha was only checked in the page), and replaying one solved captcha for many guesses | `Captcha.hpp/.cpp`, `Api::login` | `testLoginWithoutCaptchaRefused`, `testCaptchaCannotBeReplayed`, `testWrongAnswerUsesUpTheCaptcha`, `testCaptchaExpires` |
| Captcha sent only as an image | Reading the code straight from the reply | `captchaImage()` | `testCaptchaIsAnImageNotText`, `testCaptchaImageIsAValidBitmap` |
| **Unpredictable session tokens** | Guessing a token. They come from `CryptGenRandom` (Windows) or `/dev/urandom`, not `std::random_device`, which repeats itself on some MinGW versions | `SecureRandom.hpp/.cpp`, `Auth::newToken` | `testSecureRandom`, `testSessionTokensAreUnique` |
| **Sessions expire** | A stolen token working forever: 30 minutes unused, or 12 hours in total, ends a session | `Auth::find`, `Auth::expired` | `testIdleSessionExpires`, `testUseKeepsSessionAlive`, `testMaximumSessionAge`, `testExpiredTokenRejectedByApi` |
| Lockout per name | Guessing one account's password: 5 failures lock that name for 30 s | `Auth::login` | `test_api`, `test_auth` |
| **Limit per network address** | Guessing across many names from one computer: 20 failures lock that address for 5 minutes. The address comes from the connection, never from a header such as `X-Forwarded-For` | `Auth::login`, `HttpServer` | `testAddressLockedAcrossNames`, `testSuccessDoesNotResetAddressCount`, `testAddressLimitThroughApi` |
| **Plain ASCII names only** | Look-alike accounts (a Cyrillic "а" in "аdmin"). Names use A-Z, a-z, 0-9, `.`, `_`, `-`, at most 32 characters | `Auth::validName`, `opera.js` (Make a line) | `testNameRules`, `testLookAlikeNameCannotBeAdded`, `testLookAlikeNameCannotSignIn` |
| **Bounded tables** | Filling memory with made-up names or sessions: at most 10,000 entries per failure table, 10,000 sessions and 2,000 open captchas | `Auth::pruneFailures`, `Auth::pruneSessions`, `Captcha::purge` | `testFailureTableBounded`, `testSessionTableBounded`, `testCaptchaTableBounded` |
| **Request smuggling defence** | A proxy and OPERA reading a request differently: requests with `Transfer-Encoding` get 501, two different `Content-Length` values or a malformed one get 400 | `parseRequestHead` (`HttpParse.cpp`) | `testTransferEncodingRefused`, `testBadContentLengthRefused` |
| Salted SHA-256 passwords | Reading passwords from `passwords.txt` | `Auth.cpp` | `test_auth` |
| Constant-time hash comparison, same work for unknown names | Learning from timing how close a guess was, or which names exist | `Auth.cpp` | `test_auth` |
| Size limits | Huge requests: headers over 16 KB get 431, bodies over 64 KB get 413 | `HttpServer.cpp`, `HttpParse.cpp` | `testBadContentLengthRefused` |
| Path checks | Reading files outside `frontend/` (`../`) | `HttpServer.cpp` | manual |
| Roles | Operators using admin-only actions | `Api.cpp` | `test_api`, `test_api_tracking` |
| Local only by default | Other computers connecting at all, unless started with `--host 0.0.0.0` | `HttpServer.cpp`, `main.cpp` | manual |

## Known limits

- **Demo passwords are public.** The accounts in `data/passwords.txt` are in the
  docs and on GitHub. Before letting anyone else reach the server, replace every
  line using "Make a passwords.txt line" on the sign-in page.
- **A deliberately slow client can stall the server.** It handles one connection at
  a time, and the 5-second timeout applies to each read, so a client sending one
  byte every few seconds can hold it for a long time. Only reachable when the
  server is opened to a network. The full fix is an event-driven server.
- **Plain HTTP.** On a network, passwords and tokens travel unencrypted. Use HTTPS
  (for example a tunnel or a gateway in front) before using it beyond one computer.
- **The captcha slows scripts down, it does not stop a determined attacker.** A
  program that recognises the image (OCR) could still read it; the per-name and
  per-address limits are the main defence against guessing.
- **Everything is in memory.** Restarting the server ends every session and
  clears the lockout tables.
- **No two-factor sign-in.**
