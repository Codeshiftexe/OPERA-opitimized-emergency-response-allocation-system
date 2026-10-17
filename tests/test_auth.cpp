// Tests for SHA-256 and sign-in (Auth).
// Expected hashes were produced independently with Python's hashlib.

#include <set>
#include <sstream>
#include <string>

#include "Auth.hpp"
#include "CityLoader.hpp"  // LoadError
#include "Sha256.hpp"
#include "test_util.hpp"

using namespace opera;

namespace {

void testSha256KnownValues() {
    CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    // Lengths around the 64-byte block and the 56-byte padding limit.
    CHECK_EQ(sha256Hex(std::string(55, 'a')), std::string("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    CHECK_EQ(sha256Hex(std::string(56, 'a')), std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    CHECK_EQ(sha256Hex(std::string(63, 'a')), std::string("7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"));
    CHECK_EQ(sha256Hex(std::string(64, 'a')), std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
    CHECK_EQ(sha256Hex(std::string(65, 'a')), std::string("635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"));
    CHECK_EQ(sha256Hex(std::string(1000, 'a')), std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
}

// The demo accounts from the original UI's passwords.txt must keep working.
void testOriginalDemoAccounts() {
    std::istringstream in(
        "# comment\n"
        "USER admin | admin | 962c4278683b2e91 | 6b3569ee71fdf0b877fa5347eadc858878ea0538c4e1e8ca1b2f3da08733e63b\n"
        "USER operator1 | operator | 22bfce727e11c789 | f98a85b5581350271d4a23eff1c63ced863b7d774296dd739bebfec992bf7214\n");
    Auth auth;
    auth.loadUsers(in, "passwords.txt");
    CHECK_EQ(auth.userCount(), std::size_t(2));

    std::string token;
    User user;
    int wait = 0;
    CHECK(auth.login("admin", "Admin@2026", token, user, wait) == Auth::LoginResult::Ok);
    CHECK_EQ(user.role, std::string("admin"));
    CHECK(auth.login("Operator1", "Oper1@2026", token, user, wait) == Auth::LoginResult::Ok);  // name not case-sensitive
    CHECK_EQ(user.name, std::string("operator1"));
    CHECK_EQ(user.role, std::string("operator"));
    CHECK(auth.login("admin", "admin@2026", token, user, wait) == Auth::LoginResult::BadCredentials);  // password is
    CHECK(auth.login("nobody", "Admin@2026", token, user, wait) == Auth::LoginResult::BadCredentials);
}

Auth smallAuth() {
    // hashes: sha256("s1" + "pw-admin"), sha256("s2" + "pw-oper")
    std::istringstream in(
        "USER boss | admin | s1 | d534bf292ac365a54fc4c70a6048c9835e90f0845c7819ed19f3d34d36d46ab0\n"
        "USER op | operator | s2 | c759c858afb12c7b927596b80c328e34a6e07ab67d22f17f24a55ae2c5c8af5f\n");
    Auth auth;
    auth.loadUsers(in, "p.txt");
    return auth;
}

void testSessions() {
    Auth auth = smallAuth();
    std::string t1;
    std::string t2;
    User u;
    int wait = 0;
    CHECK(auth.login("boss", "pw-admin", t1, u, wait) == Auth::LoginResult::Ok);
    CHECK(auth.login("op", "pw-oper", t2, u, wait) == Auth::LoginResult::Ok);
    CHECK_EQ(t1.size(), std::size_t(64));
    CHECK(t1 != t2);
    CHECK(auth.find(t1) != nullptr);
    CHECK_EQ(auth.find(t1)->name, std::string("boss"));
    CHECK_EQ(auth.find(t2)->role, std::string("operator"));
    CHECK(auth.find("") == nullptr);
    CHECK(auth.find("not-a-token") == nullptr);
    CHECK(auth.logout(t1));
    CHECK(auth.find(t1) == nullptr);
    CHECK(!auth.logout(t1));
    CHECK(auth.find(t2) != nullptr);  // other sessions unaffected

    std::set<std::string> tokens;
    for (int i = 0; i < 50; ++i) {
        std::string t;
        auth.login("op", "pw-oper", t, u, wait);
        tokens.insert(t);
    }
    CHECK_EQ(tokens.size(), std::size_t(50));  // every token is new
}

void testLockout() {
    Auth auth = smallAuth();
    std::string t;
    User u;
    int wait = 0;
    for (int i = 0; i < Auth::kMaxFails; ++i) {
        CHECK(auth.login("op", "wrong", t, u, wait) == Auth::LoginResult::BadCredentials);
    }
    // Locked now: even the right password is refused.
    CHECK(auth.login("op", "pw-oper", t, u, wait) == Auth::LoginResult::LockedOut);
    CHECK(wait >= 1 && wait <= Auth::kLockSeconds + 1);
    // Other names are not affected.
    CHECK(auth.login("boss", "pw-admin", t, u, wait) == Auth::LoginResult::Ok);

    // A success resets the counter.
    Auth fresh = smallAuth();
    for (int i = 0; i < Auth::kMaxFails - 1; ++i) fresh.login("op", "wrong", t, u, wait);
    CHECK(fresh.login("op", "pw-oper", t, u, wait) == Auth::LoginResult::Ok);
    CHECK(fresh.login("op", "wrong", t, u, wait) == Auth::LoginResult::BadCredentials);
    CHECK(fresh.login("op", "pw-oper", t, u, wait) == Auth::LoginResult::Ok);
}

std::string loadError(const std::string& text) {
    std::istringstream in(text);
    Auth auth;
    try {
        auth.loadUsers(in, "p.txt");
    } catch (const LoadError& e) {
        return e.what();
    }
    return "";
}

void testBadFiles() {
    const std::string h(64, 'a');
    CHECK(loadError("# only comments\n").find("no USER lines") != std::string::npos);
    CHECK(loadError("USER x | boss | s | " + h + "\n").find("p.txt:1: role must be") != std::string::npos);
    CHECK(loadError("USER x | admin | s | abc\n").find("64 hex") != std::string::npos);
    CHECK(loadError("USER x | admin | s\n").find("expected") != std::string::npos);
    CHECK(loadError("USER  | admin | s | " + h + "\n").find("name is empty") != std::string::npos);
    CHECK(loadError("USER x | admin | s | " + h + "\nUSER X | operator | t | " + h + "\n").find("p.txt:2:") != std::string::npos);
    CHECK(loadError("ADMIN x\n").find("expected") != std::string::npos);
    CHECK_EQ(loadError("USER x | Admin | s | " + std::string(64, 'A') + "\n"), std::string(""));  // case tolerated
}

}  // namespace

int main() {
    testSha256KnownValues();
    testOriginalDemoAccounts();
    testSessions();
    testLockout();
    testBadFiles();
    return report("test_auth");
}
