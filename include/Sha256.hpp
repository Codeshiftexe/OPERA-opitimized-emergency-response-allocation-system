#pragma once

// SHA-256 (FIPS 180-4). Used to check passwords against passwords.txt, whose
// lines store sha256(salt + password) as 64 lowercase hex characters - the
// same format the sign-in page's "Make a passwords.txt line" tool produces.

#include <string>

namespace opera {

// Hash of the bytes of `data`, as 64 lowercase hex characters.
std::string sha256Hex(const std::string& data);

}  // namespace opera
