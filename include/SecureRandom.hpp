#pragma once

// Unpredictable random bytes from the operating system, for anything an
// attacker must not be able to guess: session tokens and captcha codes.
//
//   Windows: CryptGenRandom (wincrypt.h, advapi32). Chosen over BCryptGenRandom
//            because classic MinGW (e.g. 6.3) ships wincrypt.h but often not bcrypt.h.
//   Others:  /dev/urandom.
//
// Why not std::random_device: on some MinGW versions it returns the same
// sequence on every run, so tokens built from it can be predicted.
//
// If the operating system cannot provide random bytes, these functions throw
// std::runtime_error instead of quietly falling back to something guessable.

#include <cstddef>
#include <string>

namespace opera {

// Fills out[0..n) with random bytes, or throws std::runtime_error.
void secureRandomBytes(unsigned char* out, std::size_t n);

// `bytes` random bytes as lower-case hex (2 * bytes characters).
std::string secureRandomHex(std::size_t bytes);

// A uniformly random integer in [0, n), n > 0 (no modulo bias).
unsigned secureRandomBelow(unsigned n);

}  // namespace opera
