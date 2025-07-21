#include "utils/platform_detector.h"
#include <iostream>
#include <iomanip>

using namespace fix_gateway::utils;

void demonstrateCompileTimeDetection()
{
    std::cout << "=== COMPILE-TIME DETECTION DEMONSTRATION ===" << std::endl;
    std::cout << "These values are determined when the code is COMPILED," << std::endl;
    std::cout << "not when it's executed. They're 'baked in' to the binary." << std::endl;
    std::cout << std::endl;

    // These are resolved at compile time
    std::cout << "📋 Compile-time Platform Detection:" << std::endl;
    std::cout << "   Current Platform: " << PlatformDetector::platformToString(CURRENT_PLATFORM) << std::endl;
    std::cout << "   Current Architecture: " << PlatformDetector::architectureToString(CURRENT_ARCHITECTURE) << std::endl;
    std::cout << "   Current Compiler: " << PlatformDetector::compilerToString(CURRENT_COMPILER) << std::endl;
    std::cout << std::endl;

    // Show the preprocessor macros at work
    std::cout << "🔧 Preprocessor Macro Results:" << std::endl;
    std::cout << "   PLATFORM_MACOS: " << PLATFORM_MACOS << std::endl;
    std::cout << "   PLATFORM_LINUX: " << PLATFORM_LINUX << std::endl;
    std::cout << "   PLATFORM_WINDOWS: " << PLATFORM_WINDOWS << std::endl;
    std::cout << "   HAS_THREAD_AFFINITY: " << HAS_THREAD_AFFINITY << std::endl;
    std::cout << "   HAS_REAL_TIME_PRIORITY: " << HAS_REAL_TIME_PRIORITY << std::endl;
    std::cout << "   HAS_HUGE_PAGES: " << HAS_HUGE_PAGES << std::endl;
    std::cout << std::endl;

    // Show conditional compilation in action
    std::cout << "🚀 Conditional Compilation Examples:" << std::endl;

    // Example 1: Using traditional #ifdef
    std::cout << "   Traditional #ifdef approach:" << std::endl;
#ifdef __APPLE__
    std::cout << "     → Running on macOS - using QoS classes for thread priority" << std::endl;
#elif defined(__linux__)
    std::cout << "     → Running on Linux - using pthread_setaffinity_np for thread pinning" << std::endl;
#elif defined(_WIN32) || defined(_WIN64)
    std::cout << "     → Running on Windows - using SetThreadAffinityMask" << std::endl;
#else
    std::cout << "     → Running on Unknown platform - using fallback implementation" << std::endl;
#endif

    // Example 2: Using our convenience macros
    std::cout << "   Modern constexpr approach:" << std::endl;
    if constexpr (CURRENT_PLATFORM == PlatformType::MACOS)
    {
        std::cout << "     → Detected macOS at compile time" << std::endl;
    }
    else if constexpr (CURRENT_PLATFORM == PlatformType::LINUX)
    {
        std::cout << "     → Detected Linux at compile time" << std::endl;
    }
    else if constexpr (CURRENT_PLATFORM == PlatformType::WINDOWS)
    {
        std::cout << "     → Detected Windows at compile time" << std::endl;
    }
    else
    {
        std::cout << "     → Unknown platform at compile time" << std::endl;
    }

    std::cout << std::endl;
}

void demonstrateRuntimeDetection()
{
    std::cout << "=== RUNTIME DETECTION DEMONSTRATION ===" << std::endl;
    std::cout << "These values are determined when the program is RUNNING," << std::endl;
    std::cout << "and can provide more detailed information about the actual environment." << std::endl;
    std::cout << std::endl;

    // Get full platform information at runtime
    auto platform_info = PlatformDetector::detectPlatform();

    std::cout << "🔍 Runtime Platform Detection:" << std::endl;
    std::cout << "   Platform: " << platform_info.platform_name << std::endl;
    std::cout << "   Architecture: " << platform_info.arch_name << std::endl;
    std::cout << "   Compiler: " << platform_info.compiler_name << " (" << platform_info.compiler_version << ")" << std::endl;
    std::cout << "   Kernel: " << PlatformDetector::getKernelVersion() << std::endl;
    std::cout << "   Distribution: " << PlatformDetector::getDistributionInfo() << std::endl;
    std::cout << "   CPU Cores: " << PlatformDetector::getNumberOfCores() << std::endl;
    std::cout << std::endl;

    std::cout << "⚙️ Feature Support (Runtime Detection):" << std::endl;
    std::cout << "   Thread Pinning: " << (platform_info.supports_thread_pinning ? "✅ Yes" : "❌ No") << std::endl;
    std::cout << "   Real-time Priority: " << (platform_info.supports_real_time_priority ? "✅ Yes" : "❌ No") << std::endl;
    std::cout << "   Huge Pages: " << (platform_info.supports_huge_pages ? "✅ Yes" : "❌ No") << std::endl;
    std::cout << "   Container Environment: " << (platform_info.is_container_environment ? "✅ Yes" : "❌ No") << std::endl;
    std::cout << std::endl;
}

void demonstrateFeatureBasedProgramming()
{
    std::cout << "=== FEATURE-BASED PROGRAMMING DEMONSTRATION ===" << std::endl;
    std::cout << "This shows how to write code that adapts to platform capabilities." << std::endl;
    std::cout << std::endl;

    auto platform_info = PlatformDetector::detectPlatform();

    std::cout << "🎯 Thread Pinning Strategy:" << std::endl;
    if (platform_info.supports_thread_pinning)
    {
        std::cout << "   ✅ Thread pinning is supported!" << std::endl;
        if (platform_info.platform == PlatformType::LINUX)
        {
            std::cout << "   🔧 Using pthread_setaffinity_np for direct core assignment" << std::endl;
        }
        else if (platform_info.platform == PlatformType::WINDOWS)
        {
            std::cout << "   🔧 Using SetThreadAffinityMask for core assignment" << std::endl;
        }
    }
    else
    {
        std::cout << "   ❌ Thread pinning not supported" << std::endl;
        if (platform_info.platform == PlatformType::MACOS)
        {
            std::cout << "   🔄 Falling back to QoS classes for thread priority" << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "🚀 Real-time Priority Strategy:" << std::endl;
    if (platform_info.supports_real_time_priority)
    {
        std::cout << "   ✅ Real-time priority is supported!" << std::endl;
        if (platform_info.platform == PlatformType::LINUX)
        {
            std::cout << "   🔧 Can use SCHED_FIFO/SCHED_RR scheduling" << std::endl;
        }
        else if (platform_info.platform == PlatformType::WINDOWS)
        {
            std::cout << "   🔧 Can use REALTIME_PRIORITY_CLASS" << std::endl;
        }
    }
    else
    {
        std::cout << "   ❌ Real-time priority not supported or requires elevated privileges" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "💾 Memory Optimization Strategy:" << std::endl;
    if (platform_info.supports_huge_pages)
    {
        std::cout << "   ✅ Huge pages are supported!" << std::endl;
        std::cout << "   📈 Can use large pages for better memory performance" << std::endl;
    }
    else
    {
        std::cout << "   ❌ Huge pages not supported" << std::endl;
        std::cout << "   📊 Using standard memory allocation" << std::endl;
    }

    std::cout << std::endl;
}

void demonstrateContainerDetection()
{
    std::cout << "=== CONTAINER ENVIRONMENT DETECTION ===" << std::endl;
    std::cout << std::endl;

    auto platform_info = PlatformDetector::detectPlatform();

    if (platform_info.is_container_environment)
    {
        std::cout << "🐳 Container Environment Detected!" << std::endl;
        std::cout << "   📦 This program is running inside a container" << std::endl;
        std::cout << "   🔧 Adjusting performance expectations accordingly" << std::endl;
        std::cout << "   📊 CPU/Memory limits may be imposed by container runtime" << std::endl;
    }
    else
    {
        std::cout << "🖥️  Native Environment Detected" << std::endl;
        std::cout << "   💻 This program is running on the host OS directly" << std::endl;
        std::cout << "   🚀 Full hardware access available" << std::endl;
    }

    std::cout << std::endl;
}

int main()
{
    std::cout << "🔍 Platform Detection System Demonstration" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << std::endl;

    // Show the difference between compile-time and runtime detection
    demonstrateCompileTimeDetection();
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    demonstrateRuntimeDetection();
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    demonstrateFeatureBasedProgramming();
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    demonstrateContainerDetection();
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    // Show comprehensive platform information
    std::cout << "=== COMPREHENSIVE PLATFORM INFORMATION ===" << std::endl;
    PlatformDetector::printPlatformInfo();
    std::cout << std::endl;

    // Show exported data format
    std::cout << "=== EXPORTED PLATFORM DATA ===" << std::endl;
    auto exported_data = PlatformDetector::exportPlatformInfo();
    for (const auto &[key, value] : exported_data)
    {
        std::cout << "   " << std::left << std::setw(25) << key << ": " << value << std::endl;
    }

    std::cout << std::endl;
    std::cout << "🎯 Key Takeaways:" << std::endl;
    std::cout << "   1. Compile-time detection happens when code is built" << std::endl;
    std::cout << "   2. Runtime detection happens when program executes" << std::endl;
    std::cout << "   3. Use compile-time for performance-critical decisions" << std::endl;
    std::cout << "   4. Use runtime for dynamic environment adaptation" << std::endl;
    std::cout << "   5. Container detection helps optimize for virtualized environments" << std::endl;

    return 0;
}