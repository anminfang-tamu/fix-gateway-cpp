#pragma once

#include <string>
#include <map>

namespace fix_gateway::utils
{
    // ==================================================================
    // COMPILE-TIME PLATFORM DETECTION (Preprocessor Macros)
    // ==================================================================

    // These macros are defined by the compiler based on the target platform
    // They are evaluated at COMPILE TIME, not runtime

    enum class PlatformType
    {
        MACOS,
        LINUX,
        WINDOWS,
        FREEBSD,
        UNKNOWN
    };

    enum class ArchitectureType
    {
        X86_64,
        ARM64,
        X86_32,
        ARM32,
        UNKNOWN
    };

    enum class CompilerType
    {
        GCC,
        CLANG,
        MSVC,
        UNKNOWN
    };

    struct PlatformInfo
    {
        PlatformType platform;
        ArchitectureType architecture;
        CompilerType compiler;
        std::string platform_name;
        std::string arch_name;
        std::string compiler_name;
        std::string compiler_version;
        bool supports_thread_pinning;
        bool supports_real_time_priority;
        bool supports_huge_pages;
        bool is_container_environment;
    };

    class PlatformDetector
    {
    public:
        // ==================================================================
        // COMPILE-TIME DETECTION (Static Methods)
        // ==================================================================

        // These methods use preprocessor macros to detect platform at compile time
        static constexpr PlatformType getCompileTimePlatform()
        {
#ifdef __APPLE__
            return PlatformType::MACOS;
#elif defined(__linux__)
            return PlatformType::LINUX;
#elif defined(_WIN32) || defined(_WIN64)
            return PlatformType::WINDOWS;
#elif defined(__FreeBSD__)
            return PlatformType::FREEBSD;
#else
            return PlatformType::UNKNOWN;
#endif
        }

        static constexpr ArchitectureType getCompileTimeArchitecture()
        {
#if defined(__x86_64__) || defined(_M_X64)
            return ArchitectureType::X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
            return ArchitectureType::ARM64;
#elif defined(__i386__) || defined(_M_IX86)
            return ArchitectureType::X86_32;
#elif defined(__arm__) || defined(_M_ARM)
            return ArchitectureType::ARM32;
#else
            return ArchitectureType::UNKNOWN;
#endif
        }

        static constexpr CompilerType getCompileTimeCompiler()
        {
#ifdef __GNUC__
#ifdef __clang__
            return CompilerType::CLANG;
#else
            return CompilerType::GCC;
#endif
#elif defined(_MSC_VER)
            return CompilerType::MSVC;
#else
            return CompilerType::UNKNOWN;
#endif
        }

        // ==================================================================
        // RUNTIME DETECTION (Dynamic Methods)
        // ==================================================================

        // These methods detect platform information at runtime
        static PlatformInfo detectPlatform();
        static PlatformInfo getRuntimePlatformInfo();

        // Utility methods
        static std::string platformToString(PlatformType platform);
        static std::string architectureToString(ArchitectureType arch);
        static std::string compilerToString(CompilerType compiler);

        // Feature detection
        static bool supportsThreadPinning();
        static bool supportsRealTimePriority();
        static bool supportsHugePages();
        static bool isContainerEnvironment();

        // System information
        static int getNumberOfCores();
        static std::string getKernelVersion();
        static std::string getDistributionInfo();
        static std::string getCompilerVersion();

        // ==================================================================
        // DEBUGGING AND DIAGNOSTICS
        // ==================================================================

        // Print comprehensive platform information
        static void printPlatformInfo();
        static void printCompileTimeInfo();
        static void printRuntimeInfo();

        // Export platform info for monitoring/logging
        static std::map<std::string, std::string> exportPlatformInfo();
    };

    // ==================================================================
    // COMPILE-TIME CONSTANTS
    // ==================================================================

    // These are resolved at compile time
    constexpr PlatformType CURRENT_PLATFORM = PlatformDetector::getCompileTimePlatform();
    constexpr ArchitectureType CURRENT_ARCHITECTURE = PlatformDetector::getCompileTimeArchitecture();
    constexpr CompilerType CURRENT_COMPILER = PlatformDetector::getCompileTimeCompiler();

    // ==================================================================
    // PLATFORM-SPECIFIC FEATURE MACROS
    // ==================================================================

    // These can be used for conditional compilation
#ifdef __APPLE__
#define PLATFORM_MACOS 1
#define PLATFORM_LINUX 0
#define PLATFORM_WINDOWS 0
#define HAS_THREAD_AFFINITY 0    // Limited support
#define HAS_REAL_TIME_PRIORITY 0 // Limited support
#define HAS_HUGE_PAGES 0
#elif defined(__linux__)
#define PLATFORM_MACOS 0
#define PLATFORM_LINUX 1
#define PLATFORM_WINDOWS 0
#define HAS_THREAD_AFFINITY 1    // Full support
#define HAS_REAL_TIME_PRIORITY 1 // Full support
#define HAS_HUGE_PAGES 1
#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_MACOS 0
#define PLATFORM_LINUX 0
#define PLATFORM_WINDOWS 1
#define HAS_THREAD_AFFINITY 1    // Windows-specific API
#define HAS_REAL_TIME_PRIORITY 1 // Windows-specific API
#define HAS_HUGE_PAGES 1
#else
#define PLATFORM_MACOS 0
#define PLATFORM_LINUX 0
#define PLATFORM_WINDOWS 0
#define HAS_THREAD_AFFINITY 0
#define HAS_REAL_TIME_PRIORITY 0
#define HAS_HUGE_PAGES 0
#endif

// ==================================================================
// CONVENIENCE MACROS
// ==================================================================

// Use these for cleaner conditional compilation
#define IF_MACOS(code)                                         \
    do                                                         \
    {                                                          \
        if constexpr (CURRENT_PLATFORM == PlatformType::MACOS) \
        {                                                      \
            code                                               \
        }                                                      \
    } while (0)
#define IF_LINUX(code)                                         \
    do                                                         \
    {                                                          \
        if constexpr (CURRENT_PLATFORM == PlatformType::LINUX) \
        {                                                      \
            code                                               \
        }                                                      \
    } while (0)
#define IF_WINDOWS(code)                                         \
    do                                                           \
    {                                                            \
        if constexpr (CURRENT_PLATFORM == PlatformType::WINDOWS) \
        {                                                        \
            code                                                 \
        }                                                        \
    } while (0)

} // namespace fix_gateway::utils