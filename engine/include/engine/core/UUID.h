#pragma once
#include "engine/core/Base.h"
#include <random>
#include <functional>

namespace fw {

// 64-bit random UUID used to identify entities/assets stably across save/load
// (EnTT entity handles are not stable across sessions).
class UUID {
public:
    UUID() : m_Value(GenerateRandom()) {}
    explicit UUID(u64 value) : m_Value(value) {}

    operator u64() const { return m_Value; }
    bool operator==(const UUID& other) const { return m_Value == other.m_Value; }
    bool operator!=(const UUID& other) const { return m_Value != other.m_Value; }

    static u64 GenerateRandom() {
        static std::random_device rd;
        static std::mt19937_64 engine(rd());
        static std::uniform_int_distribution<u64> dist;
        return dist(engine);
    }

private:
    u64 m_Value;
};

} // namespace fw

namespace std {
template <> struct hash<fw::UUID> {
    std::size_t operator()(const fw::UUID& id) const noexcept {
        return std::hash<fw::u64>()(static_cast<fw::u64>(id));
    }
};
}
