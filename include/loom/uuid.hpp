#pragma once

#include <loom/result.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace loom {

struct Uuid {
    std::array<uint8_t, 16> bytes;

    static Uuid v4();
    std::string to_string() const;
    static Result<Uuid> from_string(const std::string& s);
    std::string encode_base36() const;
    static Result<Uuid> decode_base36(const std::string& s);
    bool operator==(const Uuid& other) const;
    bool operator!=(const Uuid& other) const;
};

} // namespace loom
