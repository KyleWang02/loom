#include <loom/uuid.hpp>
#include <cstring>
#include <fstream>
#include <random>

namespace loom {

// ---- RNG: /dev/urandom with mt19937_64 fallback ----

static void fill_random_bytes(uint8_t* buf, size_t len) {
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom.is_open()) {
        urandom.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
        if (static_cast<size_t>(urandom.gcount()) == len) return;
    }
    // Fallback: std::random_device + mt19937_64
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned> dist(0, 255);
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(dist(gen));
    }
}

// ---- Hex helpers ----

static const char hex_chars[] = "0123456789abcdef";

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ---- UUID v4 ----

Uuid Uuid::v4() {
    Uuid u;
    fill_random_bytes(u.bytes.data(), 16);
    // Set version 4: bytes[6] high nibble = 0100
    u.bytes[6] = (u.bytes[6] & 0x0F) | 0x40;
    // Set variant 1: bytes[8] top two bits = 10
    u.bytes[8] = (u.bytes[8] & 0x3F) | 0x80;
    return u;
}

// ---- to_string: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx ----

std::string Uuid::to_string() const {
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        out += hex_chars[bytes[i] >> 4];
        out += hex_chars[bytes[i] & 0x0F];
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            out += '-';
        }
    }
    return out;
}

// ---- from_string ----

Result<Uuid> Uuid::from_string(const std::string& s) {
    if (s.size() != 36) {
        return LoomError(LoomError::Parse,
            "UUID string must be 36 characters", "Expected format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
    }
    // Verify dash positions
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return LoomError(LoomError::Parse,
            "UUID string has invalid dash positions",
            "Expected dashes at positions 8, 13, 18, 23");
    }

    Uuid u;
    int byte_idx = 0;
    for (int i = 0; i < 36; ) {
        if (s[i] == '-') { ++i; continue; }
        int hi = hex_val(s[i]);
        int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return LoomError(LoomError::Parse,
                "UUID string contains invalid hex character",
                std::string("Invalid char at position ") + std::to_string(i));
        }
        u.bytes[byte_idx++] = static_cast<uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return Result<Uuid>::ok(u);
}

// ---- Base36 encode/decode ----
// Treat 16 bytes as a 128-bit big-endian integer.
// Repeatedly divide by 36 to extract digits.
// Fixed width: 25 characters, padded with leading '0'.

static const char base36_chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static int base36_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

// Divide a big-endian byte array (in-place) by 36, return the remainder.
static uint8_t div_by_36(uint8_t* num, size_t len) {
    uint32_t carry = 0;
    for (size_t i = 0; i < len; ++i) {
        uint32_t cur = carry * 256 + num[i];
        num[i] = static_cast<uint8_t>(cur / 36);
        carry = cur % 36;
    }
    return static_cast<uint8_t>(carry);
}

std::string Uuid::encode_base36() const {
    uint8_t work[16];
    std::memcpy(work, bytes.data(), 16);

    char buf[25];
    for (int i = 24; i >= 0; --i) {
        buf[i] = base36_chars[div_by_36(work, 16)];
    }
    return std::string(buf, 25);
}

// Multiply a big-endian byte array (in-place) by 36 and add a value.
static void mul_add_36(uint8_t* num, size_t len, uint8_t val) {
    uint32_t carry = val;
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        uint32_t cur = static_cast<uint32_t>(num[i]) * 36 + carry;
        num[i] = static_cast<uint8_t>(cur & 0xFF);
        carry = cur >> 8;
    }
}

Result<Uuid> Uuid::decode_base36(const std::string& s) {
    if (s.size() != 25) {
        return LoomError(LoomError::Parse,
            "Base36 UUID must be 25 characters",
            "Got " + std::to_string(s.size()) + " characters");
    }

    Uuid u;
    std::memset(u.bytes.data(), 0, 16);

    for (size_t i = 0; i < 25; ++i) {
        int v = base36_val(s[i]);
        if (v < 0) {
            return LoomError(LoomError::Parse,
                "Base36 UUID contains invalid character",
                std::string("Invalid char '") + s[i] + "' at position " + std::to_string(i));
        }
        mul_add_36(u.bytes.data(), 16, static_cast<uint8_t>(v));
    }
    return Result<Uuid>::ok(u);
}

// ---- Equality ----

bool Uuid::operator==(const Uuid& other) const {
    return bytes == other.bytes;
}

bool Uuid::operator!=(const Uuid& other) const {
    return bytes != other.bytes;
}

} // namespace loom
