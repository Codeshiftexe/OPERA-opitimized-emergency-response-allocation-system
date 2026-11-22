#include "Captcha.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "SecureRandom.hpp"

namespace opera {

namespace {

// No I, O, 0 or 1: they are too easy to confuse.
const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

// 5x7 pixel font: 7 rows per character, the low 5 bits of each row, left = bit 4.
struct Glyph { char c; std::uint8_t rows[7]; };
const Glyph kFont[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}}, {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}}, {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
};

const std::uint8_t* glyphRows(char c) {
    for (const Glyph& g : kFont) {
        if (g.c == c) return g.rows;
    }
    return nullptr;
}

std::string normalise(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        out += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    return out;
}

std::string base64(const std::vector<unsigned char>& data) {
    static const char* const t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                           (i + 1 < data.size() ? static_cast<unsigned>(data[i + 1]) << 8 : 0u) |
                           (i + 2 < data.size() ? static_cast<unsigned>(data[i + 2]) : 0u);
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < data.size() ? t[(v >> 6) & 63] : '=';
        out += i + 2 < data.size() ? t[v & 63] : '=';
    }
    return out;
}

void put32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
void put16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>(v >> 8));
}

}  // namespace

std::string captchaImage(const std::string& code) {
    const int W = 180, H = 60, scale = 4;
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    unsigned char seedBytes[4];
    secureRandomBytes(seedBytes, sizeof seedBytes);
    std::mt19937 rng((static_cast<unsigned>(seedBytes[0]) << 24) | (static_cast<unsigned>(seedBytes[1]) << 16) |
                     (static_cast<unsigned>(seedBytes[2]) << 8) | seedBytes[3]);
    auto rand = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto set = [&](int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
        p[0] = static_cast<unsigned char>(r); p[1] = static_cast<unsigned char>(g); p[2] = static_cast<unsigned char>(b);
    };

    for (int y = 0; y < H; ++y)                       // pale background with grain
        for (int x = 0; x < W; ++x) {
            const int n = rand(-10, 10);
            set(x, y, 232 + n, 237 + n, 242 + n);
        }

    for (std::size_t i = 0; i < code.size(); ++i) {   // the characters, each moved and slanted
        const std::uint8_t* rows = glyphRows(code[i]);
        if (rows == nullptr) continue;
        const int x0 = 10 + static_cast<int>(i) * 27 + rand(-2, 2);
        const int y0 = 14 + rand(-7, 7);
        const int shear = rand(-1, 1);                  // a slight slant: every other glyph row
        const int r = rand(10, 55), g = rand(15, 60), b = rand(30, 90);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col) {
                if (!(rows[row] & (0x10 >> col))) continue;
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) {
                        const int yy = y0 + row * scale + dy;
                        set(x0 + col * scale + dx + shear * ((3 - row) / 2), yy, r, g, b);
                    }
            }
    }

    for (int k = 0; k < 2; ++k) {                     // two thin, pale crossing lines:
        int x = 0, y = rand(8, H - 8);                // enough to upset simple OCR,
        const int x1 = W - 1, y1 = rand(8, H - 8);    // too faint to look like a stroke
        const int v = rand(150, 190);
        for (int s = 0; s <= W; ++s) {
            set(x + (x1 - x) * s / W, y + (y1 - y) * s / W, v, v + 5, v + 12);
        }
    }
    for (int k = 0; k < 220; ++k) {                   // light speckles
        const int v = rand(150, 205);
        set(rand(0, W - 1), rand(0, H - 1), v, v, v);
    }

    // 24-bit BMP (rows bottom-up, BGR, each row padded to 4 bytes).
    const int rowBytes = (W * 3 + 3) & ~3;
    const std::uint32_t imageBytes = static_cast<std::uint32_t>(rowBytes) * H;
    std::vector<unsigned char> bmp;
    bmp.reserve(54 + imageBytes);
    bmp.push_back('B'); bmp.push_back('M');
    put32(bmp, 54 + imageBytes); put32(bmp, 0); put32(bmp, 54);
    put32(bmp, 40); put32(bmp, W); put32(bmp, H); put16(bmp, 1); put16(bmp, 24);
    put32(bmp, 0); put32(bmp, imageBytes); put32(bmp, 2835); put32(bmp, 2835); put32(bmp, 0); put32(bmp, 0);
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
            bmp.push_back(p[2]); bmp.push_back(p[1]); bmp.push_back(p[0]);
        }
        for (int pad = W * 3; pad < rowBytes; ++pad) bmp.push_back(0);
    }
    return "data:image/bmp;base64," + base64(bmp);
}

void Captcha::purge(Clock::time_point now) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.expires <= now) it = pending_.erase(it);
        else ++it;
    }
    while (pending_.size() >= kMaxPending) {          // still full: drop the oldest
        auto oldest = std::min_element(pending_.begin(), pending_.end(),
            [](const std::pair<const std::string, Entry>& a, const std::pair<const std::string, Entry>& b) {
                return a.second.expires < b.second.expires;
            });
        pending_.erase(oldest);
    }
}

CaptchaChallenge Captcha::issue() {
    const auto now = Clock::now();
    purge(now);
    std::string code;
    for (int i = 0; i < kLength; ++i) code += kAlphabet[secureRandomBelow(sizeof kAlphabet - 1)];
    CaptchaChallenge c;
    c.id = secureRandomHex(16);
    c.image = captchaImage(code);
    pending_[c.id] = Entry{code, now + std::chrono::seconds(lifetimeSeconds_)};
    return c;
}

bool Captcha::check(const std::string& id, const std::string& answer) {
    const auto it = pending_.find(id);
    if (it == pending_.end()) return false;
    const Entry e = it->second;
    pending_.erase(it);                               // single use, right or wrong
    if (Clock::now() >= e.expires) return false;
    return !answer.empty() && normalise(answer) == e.code;
}

std::string Captcha::answerForTest(const std::string& id) const {
    const auto it = pending_.find(id);
    return it == pending_.end() ? std::string() : it->second.code;
}

}  // namespace opera
