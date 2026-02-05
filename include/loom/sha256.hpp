#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <filesystem>

namespace loom {

class SHA256 {
public:
    SHA256();

    // Feed data in chunks
    void update(const uint8_t* data, size_t len);
    void update(const std::string& s);

    // Finalize and return the 32-byte digest. Object should not be
    // reused after this call.
    std::array<uint8_t, 32> finalize();

    // One-shot helpers
    static std::string hash_hex(const std::string& input);
    static std::string hash_file(const std::filesystem::path& path);
    static std::string bytes_to_hex(const std::array<uint8_t, 32>& bytes);

private:
    void process_block(const uint8_t block[64]);

    std::array<uint32_t, 8> state_;   // H0..H7
    uint64_t total_bytes_;             // total bytes fed so far
    uint8_t  buffer_[64];              // partial block accumulator
    size_t   buffer_len_;              // bytes in buffer_
};

} // namespace loom
