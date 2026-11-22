#include "SecureRandom.hpp"

#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#ifndef CRYPT_SILENT
#define CRYPT_SILENT 0x00000040   // older MinGW headers may not define it
#endif
#else
#include <fstream>
#endif

namespace opera {

void secureRandomBytes(unsigned char* out, std::size_t n) {
    if (n == 0) return;
#ifdef _WIN32
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        throw std::runtime_error("the system random generator is not available (CryptAcquireContext)");
    }
    const BOOL ok = CryptGenRandom(provider, static_cast<DWORD>(n), out);
    CryptReleaseContext(provider, 0);
    if (!ok) throw std::runtime_error("the system random generator failed (CryptGenRandom)");
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n))) {
        throw std::runtime_error("the system random generator is not available (/dev/urandom)");
    }
#endif
}

std::string secureRandomHex(std::size_t bytes) {
    static const char* const kHex = "0123456789abcdef";
    std::string raw(bytes, '\0');
    secureRandomBytes(reinterpret_cast<unsigned char*>(&raw[0]), bytes);
    std::string hex;
    hex.reserve(bytes * 2);
    for (unsigned char c : raw) {
        hex += kHex[c >> 4];
        hex += kHex[c & 15];
    }
    return hex;
}

unsigned secureRandomBelow(unsigned n) {
    if (n == 0) throw std::invalid_argument("secureRandomBelow(0)");
    // Reject values from the incomplete last block, so every result is equally likely.
    const unsigned long long range = 0x100000000ULL;          // 2^32
    const unsigned long long limit = range - (range % n);
    while (true) {
        unsigned char b[4];
        secureRandomBytes(b, sizeof b);
        const unsigned long long v = (static_cast<unsigned long long>(b[0]) << 24) |
                                     (static_cast<unsigned long long>(b[1]) << 16) |
                                     (static_cast<unsigned long long>(b[2]) << 8) | b[3];
        if (v < limit) return static_cast<unsigned>(v % n);
    }
}

}  // namespace opera
