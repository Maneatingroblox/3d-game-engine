#pragma once
// Core-wide includes, platform detection and small utility macros used across
// every module of the engine.

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

#if defined(_WIN32)
    #define FW_PLATFORM_WINDOWS 1
#else
    #define FW_PLATFORM_WINDOWS 0
#endif

// FORGEWORKS_HEADLESS is defined by CMake when building the platform-independent
// core (no D3D11 / Win32 / audio) for cross-platform validation and unit tests.
#if !defined(FORGEWORKS_HEADLESS)
    #define FORGEWORKS_HEADLESS 0
#endif

namespace fw {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

template <typename T>
using Scope = std::unique_ptr<T>;

template <typename T, typename... Args>
constexpr Scope<T> MakeScope(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
using Ref = std::shared_ptr<T>;

template <typename T, typename... Args>
constexpr Ref<T> MakeRef(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

} // namespace fw

// Short commit this binary was built from (set by CMakeLists.txt; logged at
// start-up and shown in the window title so a report identifies its build).
#if !defined(FW_BUILD_ID)
    #define FW_BUILD_ID "unknown"
#endif

#define FW_BIT(x) (1u << (x))
#define FW_UNUSED(x) (void)(x)

#if defined(_MSC_VER)
    #define FW_DEBUGBREAK() __debugbreak()
#else
    #include <csignal>
    #define FW_DEBUGBREAK() raise(SIGTRAP)
#endif
