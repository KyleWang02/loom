#include <loom/sha256.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace loom {

// ---- Constants (first 32 bits of the fractional parts of the cube roots
//      of the first 64 primes, FIPS 180-4 section 4.2.2) ----

static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// ---- Bit manipulation helpers ----

static inline uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t big_sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static inline uint32_t big_sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static inline uint32_t small_sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t small_sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

// Read a big-endian 32-bit word from a byte pointer.
static inline uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24)
         | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) <<  8)
         | (uint32_t(p[3]));
}

// Write a big-endian 32-bit word to a byte pointer.
static inline void write_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >>  8);
    p[3] = uint8_t(v);
}

// Write a big-endian 64-bit word to a byte pointer.
static inline void write_be64(uint8_t* p, uint64_t v) {
    p[0] = uint8_t(v >> 56);
    p[1] = uint8_t(v >> 48);
    p[2] = uint8_t(v >> 40);
    p[3] = uint8_t(v >> 32);
    p[4] = uint8_t(v >> 24);
    p[5] = uint8_t(v >> 16);
    p[6] = uint8_t(v >>  8);
    p[7] = uint8_t(v);
}

// ---- SHA256 implementation ----

SHA256::SHA256() {
    // Initial hash values (first 32 bits of the fractional parts of the
    // square roots of the first 8 primes, FIPS 180-4 section 5.3.3).
    state_ = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    total_bytes_ = 0;
    buffer_len_ = 0;
}

void SHA256::process_block(const uint8_t block[64]) {
    // 1. Prepare the message schedule W[0..63].
    uint32_t W[64];
    for (int t = 0; t < 16; ++t) {
        W[t] = read_be32(block + t * 4);
    }
    for (int t = 16; t < 64; ++t) {
        W[t] = small_sigma1(W[t-2]) + W[t-7]
             + small_sigma0(W[t-15]) + W[t-16];
    }

    // 2. Initialize working variables.
    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    // 3. Compression: 64 rounds.
    for (int t = 0; t < 64; ++t) {
        uint32_t T1 = h + big_sigma1(e) + ch(e, f, g) + K[t] + W[t];
        uint32_t T2 = big_sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    // 4. Update state.
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void SHA256::update(const uint8_t* data, size_t len) {
    total_bytes_ += len;

    // If there is leftover data in the buffer, try to fill it to 64 bytes.
    if (buffer_len_ > 0) {
        size_t space = 64 - buffer_len_;
        size_t copy = (len < space) ? len : space;
        std::memcpy(buffer_ + buffer_len_, data, copy);
        buffer_len_ += copy;
        data += copy;
        len -= copy;

        if (buffer_len_ == 64) {
            process_block(buffer_);
            buffer_len_ = 0;
        }
    }

    // Process full 64-byte blocks directly from the input.
    while (len >= 64) {
        process_block(data);
        data += 64;
        len -= 64;
    }

    // Stash any remaining bytes.
    if (len > 0) {
        std::memcpy(buffer_, data, len);
        buffer_len_ = len;
    }
}

void SHA256::update(const std::string& s) {
    update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::array<uint8_t, 32> SHA256::finalize() {
    // Pad the message per FIPS 180-4 section 5.1.1:
    //   append bit '1' (byte 0x80)
    //   append zero bytes until length ≡ 56 mod 64
    //   append original length in bits as 64-bit big-endian

    uint64_t total_bits = total_bytes_ * 8;

    // Append the 0x80 byte.
    uint8_t pad_byte = 0x80;
    update(&pad_byte, 1);

    // Pad with zeros until buffer_len_ == 56.
    // If buffer_len_ is already past 56, we need to fill this block
    // with zeros, process it, then pad another block to 56.
    if (buffer_len_ > 56) {
        while (buffer_len_ < 64) {
            uint8_t zero = 0;
            // Directly write to buffer to avoid re-incrementing total_bytes_.
            buffer_[buffer_len_++] = 0;
        }
        process_block(buffer_);
        buffer_len_ = 0;
    }
    while (buffer_len_ < 56) {
        buffer_[buffer_len_++] = 0;
    }

    // Append the total bit count as big-endian 64-bit.
    write_be64(buffer_ + 56, total_bits);
    process_block(buffer_);

    // Produce the 32-byte digest from state_.
    std::array<uint8_t, 32> digest;
    for (int i = 0; i < 8; ++i) {
        write_be32(digest.data() + i * 4, state_[i]);
    }
    return digest;
}

std::string SHA256::bytes_to_hex(const std::array<uint8_t, 32>& bytes) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t b : bytes) {
        out += hex_chars[b >> 4];
        out += hex_chars[b & 0x0f];
    }
    return out;
}

std::string SHA256::hash_hex(const std::string& input) {
    SHA256 ctx;
    ctx.update(input);
    return bytes_to_hex(ctx.finalize());
}

std::string SHA256::hash_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return "";

    SHA256 ctx;
    char buf[8192];
    while (in.read(buf, sizeof(buf))) {
        ctx.update(reinterpret_cast<const uint8_t*>(buf),
                   static_cast<size_t>(in.gcount()));
    }
    // Handle the last partial read
    if (in.gcount() > 0) {
        ctx.update(reinterpret_cast<const uint8_t*>(buf),
                   static_cast<size_t>(in.gcount()));
    }
    return bytes_to_hex(ctx.finalize());
}

} // namespace loom
