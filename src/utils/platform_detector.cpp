#include "utils/platform_detector.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

namespace fix_gateway::utils
{
    // ==================================================================
    // PLATFORM INFORMATION DETECTION
    // ==================================================================

    PlatformInfo PlatformDetector::detectPlatform()
    {
        PlatformInfo info;

        // Compile-time detection
        info.platform = getCompileTimePlatform();
        info.architecture = getCompileTimeArchitecture();
        info.compiler = getCompileTimeCompiler();

        // String representations
        info.platform_name = platformToString(info.platform);
        info.arch_name = architectureToString(info.architecture);
        info.compiler_name = compilerToString(info.compiler);

        // Compiler version
        info.compiler_version = getCompilerVersion();

        // Feature support
        info.supports_thread_pinning = supportsThreadPinning();
        info.supports_real_time_priority = supportsRealTimePriority();
        info.supports_huge_pages = supportsHugePages();
        info.is_container_environment = isContainerEnvironment();

        return info;
    }

    PlatformInfo PlatformDetector::getRuntimePlatformInfo()
    {
        return detectPlatform();
    }

    // ==================================================================
    // STRING CONVERSION METHODS
    // ==================================================================

    std::string PlatformDetector::platformToString(PlatformType platform)
    {
        switch (platform)
        {
        case PlatformType::MACOS:
            return "macOS";
        case PlatformType::LINUX:
            return "Linux";
        case PlatformType::WINDOWS:
            return "Windows";
        case PlatformType::FREEBSD:
            return "FreeBSD";
        case PlatformType::UNKNOWN:
            return "Unknown";
        default:
            return "Unknown";
        }
    }

    std::string PlatformDetector::architectureToString(ArchitectureType arch)
    {
        switch (arch)
        {
        case ArchitectureType::X86_64:
            return "x86_64";
        case ArchitectureType::ARM64:
            return "ARM64";
        case ArchitectureType::X86_32:
            return "x86_32";
        case ArchitectureType::ARM32:
            return "ARM32";
        case ArchitectureType::UNKNOWN:
            return "Unknown";
        default:
            return "Unknown";
        }
    }

    std::string PlatformDetector::compilerToString(CompilerType compiler)
    {
        switch (compiler)
        {
        case CompilerType::GCC:
            return "GCC";
        case CompilerType::CLANG:
            return "Clang";
        case CompilerType::MSVC:
            return "MSVC";
        case CompilerType::UNKNOWN:
            return "Unknown";
        default:
            return "Unknown";
        }
    }

    // ==================================================================
    // FEATURE DETECTION
    // ==================================================================

    bool PlatformDetector::supportsThreadPinning()
    {
        // Compile-time detection of thread pinning support
#ifdef __linux__
        return true; // Linux has full pthread_setaffinity_np support
#elif defined(__APPLE__)
        return false; // macOS has limited support (QoS classes only)
#elif defined(_WIN32) || defined(_WIN64)
        return true; // Windows has SetThreadAffinityMask
#else
        return false;
#endif
    }

    bool PlatformDetector::supportsRealTimePriority()
    {
        // Check if real-time priority is supported
#ifdef __linux__
        // Linux supports SCHED_FIFO/SCHED_RR
        return (getuid() == 0); // Needs root privileges
#elif defined(__APPLE__)
        // macOS has limited real-time support
        return false;
#elif defined(_WIN32) || defined(_WIN64)
        // Windows has real-time priority classes
        return true;
#else
        return false;
#endif
    }

    bool PlatformDetector::supportsHugePages()
    {
        // Check for huge pages support
#ifdef __linux__
        // Check if /sys/kernel/mm/hugepages exists
        std::ifstream hugepages("/sys/kernel/mm/hugepages");
        return hugepages.good();
#elif defined(__APPLE__)
        return false; // macOS doesn't support huge pages
#elif defined(_WIN32) || defined(_WIN64)
        return true; // Windows has large page support
#else
        return false;
#endif
    }

    bool PlatformDetector::isContainerEnvironment()
    {
        // Multiple ways to detect container environment

        // Check for /.dockerenv file (Docker)
        std::ifstream dockerenv("/.dockerenv");
        if (dockerenv.good())
        {
            return true;
        }

        // Check cgroup information
        std::ifstream cgroup("/proc/1/cgroup");
        if (cgroup.good())
        {
            std::string line;
            while (std::getline(cgroup, line))
            {
                if (line.find("docker") != std::string::npos ||
                    line.find("containerd") != std::string::npos ||
                    line.find("kubepods") != std::string::npos)
                {
                    return true;
                }
            }
        }

        // Check for container-specific environment variables
        if (std::getenv("CONTAINER") != nullptr ||
            std::getenv("DOCKER_CONTAINER") != nullptr ||
            std::getenv("KUBERNETES_SERVICE_HOST") != nullptr)
        {
            return true;
        }

        return false;
    }

    // ==================================================================
    // SYSTEM INFORMATION
    // ==================================================================

    int PlatformDetector::getNumberOfCores()
    {
        return std::thread::hardware_concurrency();
    }

    std::string PlatformDetector::getKernelVersion()
    {
        struct utsname buffer;
        if (uname(&buffer) == 0)
        {
            return std::string(buffer.release);
        }
        return "Unknown";
    }

    std::string PlatformDetector::getDistributionInfo()
    {
#ifdef __linux__
        // Try to read /etc/os-release
        std::ifstream osrelease("/etc/os-release");
        if (osrelease.good())
        {
            std::string line;
            while (std::getline(osrelease, line))
            {
                if (line.find("PRETTY_NAME=") == 0)
                {
                    return line.substr(13, line.length() - 14); // Remove quotes
                }
            }
        }

        // Fallback to uname
        struct utsname buffer;
        if (uname(&buffer) == 0)
        {
            return std::string(buffer.sysname) + " " + std::string(buffer.release);
        }
#elif defined(__APPLE__)
        // macOS version detection
        struct utsname buffer;
        if (uname(&buffer) == 0)
        {
            return std::string(buffer.sysname) + " " + std::string(buffer.release);
        }
#endif
        return "Unknown";
    }

    std::string PlatformDetector::getCompilerVersion()
    {
        std::stringstream version;

#ifdef __GNUC__
#ifdef __clang__
        version << "Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#else
        version << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#endif
#elif defined(_MSC_VER)
        version << "MSVC " << _MSC_VER;
#else
        version << "Unknown";
#endif

        return version.str();
    }

    // ==================================================================
    // DEBUGGING AND DIAGNOSTICS
    // ==================================================================

    void PlatformDetector::printPlatformInfo()
    {
        std::cout << "=== Platform Detection Information ===" << std::endl;
        printCompileTimeInfo();
        std::cout << std::endl;
        printRuntimeInfo();
    }

    void PlatformDetector::printCompileTimeInfo()
    {
        std::cout << "📋 COMPILE-TIME DETECTION:" << std::endl;
        std::cout << "   Platform: " << platformToString(getCompileTimePlatform()) << std::endl;
        std::cout << "   Architecture: " << architectureToString(getCompileTimeArchitecture()) << std::endl;
        std::cout << "   Compiler: " << compilerToString(getCompileTimeCompiler()) << std::endl;

        std::cout << "🔧 FEATURE SUPPORT (Compile-time):" << std::endl;
        std::cout << "   Thread Pinning: " << (HAS_THREAD_AFFINITY ? "✅ Yes" : "❌ No") << std::endl;
        std::cout << "   Real-time Priority: " << (HAS_REAL_TIME_PRIORITY ? "✅ Yes" : "❌ No") << std::endl;
        std::cout << "   Huge Pages: " << (HAS_HUGE_PAGES ? "✅ Yes" : "❌ No") << std::endl;

        std::cout << "🏗️ PREPROCESSOR MACROS:" << std::endl;
        std::cout << "   __APPLE__: " << (PLATFORM_MACOS ? "defined" : "undefined") << std::endl;
        std::cout << "   __linux__: " << (PLATFORM_LINUX ? "defined" : "undefined") << std::endl;
        std::cout << "   _WIN32/_WIN64: " << (PLATFORM_WINDOWS ? "defined" : "undefined") << std::endl;
    }

    void PlatformDetector::printRuntimeInfo()
    {
        auto info = detectPlatform();

        std::cout << "🚀 RUNTIME DETECTION:" << std::endl;
        std::cout << "   Platform: " << info.platform_name << std::endl;
        std::cout << "   Architecture: " << info.arch_name << std::endl;
        std::cout << "   Compiler: " << info.compiler_name << " (" << info.compiler_version << ")" << std::endl;
        std::cout << "   Kernel: " << getKernelVersion() << std::endl;
        std::cout << "   Distribution: " << getDistributionInfo() << std::endl;
        std::cout << "   CPU Cores: " << getNumberOfCores() << std::endl;

        std::cout << "⚙️ FEATURE SUPPORT (Runtime):" << std::endl;
        std::cout << "   Thread Pinning: " << (info.supports_thread_pinning ? "✅ Yes" : "❌ No") << std::endl;
        std::cout << "   Real-time Priority: " << (info.supports_real_time_priority ? "✅ Yes" : "❌ No") << std::endl;
        std::cout << "   Huge Pages: " << (info.supports_huge_pages ? "✅ Yes" : "❌ No") << std::endl;
        std::cout << "   Container Environment: " << (info.is_container_environment ? "✅ Yes" : "❌ No") << std::endl;
    }

    std::map<std::string, std::string> PlatformDetector::exportPlatformInfo()
    {
        auto info = detectPlatform();

        std::map<std::string, std::string> export_data;
        export_data["platform"] = info.platform_name;
        export_data["architecture"] = info.arch_name;
        export_data["compiler"] = info.compiler_name;
        export_data["compiler_version"] = info.compiler_version;
        export_data["kernel_version"] = getKernelVersion();
        export_data["distribution"] = getDistributionInfo();
        export_data["cpu_cores"] = std::to_string(getNumberOfCores());
        export_data["supports_thread_pinning"] = info.supports_thread_pinning ? "true" : "false";
        export_data["supports_real_time_priority"] = info.supports_real_time_priority ? "true" : "false";
        export_data["supports_huge_pages"] = info.supports_huge_pages ? "true" : "false";
        export_data["is_container_environment"] = info.is_container_environment ? "true" : "false";

        return export_data;
    }

} // namespace fix_gateway::utils