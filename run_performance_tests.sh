#!/bin/bash

# StreamFixParser Performance Test Runner for Linux
# This script runs comprehensive performance tests with system optimizations

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build"
TEST_EXECUTABLE="tests/test_stream_fix_parser_performance"
RESULTS_DIR="performance_results"
LOG_FILE="performance_test.log"

echo -e "${BLUE}=== StreamFixParser Performance Test Runner ===${NC}"
echo -e "${BLUE}Running comprehensive performance tests on Linux${NC}"
echo ""

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    print_warning "This script is optimized for Linux. Some features may not work on other platforms."
fi

# Function to check system requirements
check_system_requirements() {
    print_status "Checking system requirements..."
    
    # Check CPU info
    if [ -f /proc/cpuinfo ]; then
        CPU_CORES=$(nproc)
        CPU_MODEL=$(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
        print_status "CPU: $CPU_MODEL ($CPU_CORES cores)"
    fi
    
    # Check memory
    if [ -f /proc/meminfo ]; then
        TOTAL_MEM=$(grep MemTotal /proc/meminfo | awk '{print $2}')
        TOTAL_MEM_GB=$((TOTAL_MEM / 1024 / 1024))
        print_status "Total Memory: ${TOTAL_MEM_GB}GB"
    fi
    
    # Check for performance monitoring tools
    if command -v perf &> /dev/null; then
        PERF_AVAILABLE=true
        print_status "perf monitoring available"
    else
        PERF_AVAILABLE=false
        print_warning "perf not available - install linux-tools-$(uname -r) for detailed profiling"
    fi
    
    # Check for time command
    if command -v /usr/bin/time &> /dev/null; then
        TIME_AVAILABLE=true
        print_status "/usr/bin/time available for detailed resource monitoring"
    else
        TIME_AVAILABLE=false
        print_warning "/usr/bin/time not available"
    fi
    
    echo ""
}

# Function to optimize system for performance testing
optimize_system() {
    print_status "Applying system optimizations for performance testing..."
    
    # Set CPU governor to performance (requires root)
    if [ -w /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
        print_status "Setting CPU governor to performance mode..."
        for cpu in /sys/devices/cpu/cpu*/cpufreq/scaling_governor; do
            if [ -w "$cpu" ]; then
                echo performance > "$cpu" 2>/dev/null || true
            fi
        done
    else
        print_warning "Cannot set CPU governor (requires root). Performance may vary."
    fi
    
    # Disable swapping temporarily if possible
    if command -v swapoff &> /dev/null && [ "$EUID" -eq 0 ]; then
        print_status "Temporarily disabling swap..."
        swapoff -a 2>/dev/null || true
    else
        print_warning "Cannot disable swap (requires root). Memory performance may be affected."
    fi
    
    # Set high priority for better scheduling
    if command -v nice &> /dev/null; then
        NICE_COMMAND="nice -n -10"
        print_status "Will run tests with high priority (nice -10)"
    else
        NICE_COMMAND=""
    fi
    
    echo ""
}

# Function to restore system settings
restore_system() {
    print_status "Restoring system settings..."
    
    # Re-enable swap if we disabled it
    if command -v swapon &> /dev/null && [ "$EUID" -eq 0 ]; then
        swapon -a 2>/dev/null || true
    fi
    
    # Restore CPU governor to ondemand/schedutil
    if [ -w /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
        for cpu in /sys/devices/cpu/cpu*/cpufreq/scaling_governor; do
            if [ -w "$cpu" ]; then
                echo ondemand > "$cpu" 2>/dev/null || echo schedutil > "$cpu" 2>/dev/null || true
            fi
        done
    fi
    
    echo ""
}

# Function to build the project
build_project() {
    print_status "Building project in release mode..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        mkdir -p "$BUILD_DIR"
    fi
    
    cd "$BUILD_DIR"
    
    # Configure with release optimizations
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-O3 -march=native -DNDEBUG -flto" \
          -DBUILD_TESTING=ON \
          .. || { print_error "CMake configuration failed"; exit 1; }
    
    # Build with all available cores
    make -j$CPU_CORES || { print_error "Build failed"; exit 1; }
    
    cd ..
    
    # Verify test executable exists
    if [ ! -f "$BUILD_DIR/$TEST_EXECUTABLE" ]; then
        print_error "Performance test executable not found: $BUILD_DIR/$TEST_EXECUTABLE"
        exit 1
    fi
    
    print_status "Build completed successfully"
    echo ""
}

# Function to run basic functionality test first
run_functionality_test() {
    print_status "Running basic functionality test first..."
    
    if [ -f "$BUILD_DIR/tests/test_stream_fix_parser" ]; then
        cd "$BUILD_DIR"
        ./tests/test_stream_fix_parser || { 
            print_error "Basic functionality test failed. Aborting performance tests."
            cd ..
            exit 1
        }
        cd ..
        print_status "Basic functionality test passed"
    else
        print_warning "Basic functionality test not available"
    fi
    
    echo ""
}

# Function to run performance tests with monitoring
run_performance_tests() {
    print_status "Running performance tests..."
    
    # Create results directory
    mkdir -p "$RESULTS_DIR"
    
    cd "$BUILD_DIR"
    
    # Prepare monitoring commands
    if [ "$TIME_AVAILABLE" = true ]; then
        TIME_CMD="/usr/bin/time -v"
    else
        TIME_CMD="time"
    fi
    
    if [ "$PERF_AVAILABLE" = true ]; then
        PERF_CMD="perf stat -e cycles,instructions,cache-references,cache-misses,branch-misses"
        print_status "Running with perf monitoring..."
    else
        PERF_CMD=""
    fi
    
    # Run the performance test
    print_status "Executing performance test suite..."
    
    if [ -n "$PERF_CMD" ]; then
        # Run with perf monitoring
        $NICE_COMMAND $PERF_CMD $TIME_CMD ./$TEST_EXECUTABLE 2>&1 | tee "../$RESULTS_DIR/$LOG_FILE"
    else
        # Run with basic time monitoring
        $NICE_COMMAND $TIME_CMD ./$TEST_EXECUTABLE 2>&1 | tee "../$RESULTS_DIR/$LOG_FILE"
    fi
    
    TEST_EXIT_CODE=${PIPESTATUS[0]}
    
    cd ..
    
    if [ $TEST_EXIT_CODE -eq 0 ]; then
        print_status "Performance tests completed successfully"
    else
        print_error "Performance tests failed with exit code: $TEST_EXIT_CODE"
        return 1
    fi
    
    echo ""
}

# Function to collect system information
collect_system_info() {
    print_status "Collecting system information..."
    
    SYSINFO_FILE="$RESULTS_DIR/system_info.txt"
    
    {
        echo "System Information for Performance Test"
        echo "======================================="
        echo "Date: $(date)"
        echo "Hostname: $(hostname)"
        echo "Kernel: $(uname -r)"
        echo "OS: $(lsb_release -d 2>/dev/null | cut -f2 || echo "Unknown")"
        echo ""
        
        echo "CPU Information:"
        if [ -f /proc/cpuinfo ]; then
            echo "  Model: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
            echo "  Cores: $(nproc)"
            echo "  Architecture: $(uname -m)"
            if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq ]; then
                FREQ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
                echo "  Current Frequency: $((FREQ / 1000)) MHz"
            fi
        fi
        echo ""
        
        echo "Memory Information:"
        if [ -f /proc/meminfo ]; then
            grep -E "MemTotal|MemFree|MemAvailable|SwapTotal|SwapFree" /proc/meminfo
        fi
        echo ""
        
        echo "Compiler Information:"
        g++ --version | head -1 2>/dev/null || echo "g++ not available"
        echo ""
        
        echo "Build Configuration:"
        echo "  Build Type: Release"
        echo "  Optimization: -O3 -march=native -DNDEBUG -flto"
        echo "  Threading: Enabled"
        echo ""
        
    } > "$SYSINFO_FILE"
    
    print_status "System information saved to: $SYSINFO_FILE"
    echo ""
}

# Function to analyze results
analyze_results() {
    print_status "Analyzing performance results..."
    
    # Look for the generated performance results file
    PERF_RESULT_FILE=$(find "$RESULTS_DIR" -name "stream_fix_parser_performance_*.txt" -type f | head -1)
    
    if [ -n "$PERF_RESULT_FILE" ] && [ -f "$PERF_RESULT_FILE" ]; then
        print_status "Performance results found: $(basename "$PERF_RESULT_FILE")"
        
        # Extract key metrics
        if grep -q "Single-Threaded Throughput" "$PERF_RESULT_FILE"; then
            THROUGHPUT=$(grep "Throughput (msgs/sec)" "$PERF_RESULT_FILE" | head -1 | awk '{print $3}')
            print_status "Peak Throughput: $THROUGHPUT messages/second"
        fi
        
        if grep -q "Average latency" "$PERF_RESULT_FILE"; then
            AVG_LATENCY=$(grep "Average latency" "$PERF_RESULT_FILE" | head -1 | awk '{print $3}')
            print_status "Average Latency: $AVG_LATENCY nanoseconds"
        fi
        
        # Copy results to timestamped location
        cp "$PERF_RESULT_FILE" "$RESULTS_DIR/"
        
    else
        print_warning "Performance results file not found"
    fi
    
    # Copy log file to results directory
    if [ -f "$RESULTS_DIR/$LOG_FILE" ]; then
        print_status "Test log saved to: $RESULTS_DIR/$LOG_FILE"
    fi
    
    echo ""
}

# Function to display summary
display_summary() {
    echo -e "${GREEN}=== Performance Test Summary ===${NC}"
    echo ""
    
    if [ -d "$RESULTS_DIR" ]; then
        echo "📁 Results Directory: $RESULTS_DIR/"
        echo "   Available files:"
        ls -la "$RESULTS_DIR/" | grep -v "^total" | awk '{print "   " $9 " (" $5 " bytes)"}'
        echo ""
    fi
    
    echo "🔧 Recommendations for production deployment:"
    echo "   - Use Release build (-O3 optimization)"
    echo "   - Set CPU governor to 'performance'"
    echo "   - Disable swap for consistent latency"
    echo "   - Use CPU affinity for critical threads"
    echo "   - Monitor memory usage and tune pool sizes"
    echo ""
    
    echo "📊 For detailed analysis:"
    echo "   - Check the performance results file"
    echo "   - Review system information"
    echo "   - Compare with baseline measurements"
    echo ""
}

# Main execution flow
main() {
    # Setup trap to restore system settings on exit
    trap restore_system EXIT
    
    # Check requirements
    check_system_requirements
    
    # Collect system info
    collect_system_info
    
    # Optimize system
    optimize_system
    
    # Build project
    build_project
    
    # Run basic test first
    run_functionality_test
    
    # Run performance tests
    if run_performance_tests; then
        # Analyze results
        analyze_results
        
        # Display summary
        display_summary
        
        print_status "Performance testing completed successfully!"
        exit 0
    else
        print_error "Performance testing failed!"
        exit 1
    fi
}

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi 