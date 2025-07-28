#!/bin/bash

# StreamFixParser Performance Test Deployment Script
# This script deploys and runs performance tests using Docker

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
PERFORMANCE_COMPOSE_FILE="docker-compose.performance.yml"
RESULTS_DIR="performance_results"
CONTAINER_NAME="fix-parser-performance-test"
DOCKERFILE="Dockerfile.performance"

echo -e "${BLUE}=== StreamFixParser Performance Test Deployment ===${NC}"
echo -e "${BLUE}Deploying and running performance tests using Docker${NC}"
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

print_step() {
    echo -e "${CYAN}[STEP]${NC} $1"
}

# Function to check Docker installation
check_docker() {
    print_step "Checking Docker installation..."
    
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed. Please install Docker first."
        echo "Visit: https://docs.docker.com/get-docker/"
        exit 1
    fi
    
    if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
        print_error "Docker Compose is not available. Please install Docker Compose."
        exit 1
    fi
    
    # Check if Docker daemon is running
    if ! docker info &> /dev/null; then
        print_error "Docker daemon is not running. Please start Docker."
        exit 1
    fi
    
    # Determine compose command
    if docker compose version &> /dev/null 2>&1; then
        COMPOSE_CMD="docker compose"
    elif command -v docker-compose &> /dev/null; then
        COMPOSE_CMD="docker-compose"
    else
        print_error "Neither 'docker compose' nor 'docker-compose' is available."
        exit 1
    fi
    
    print_status "Docker and Compose are available"
    print_status "Using compose command: $COMPOSE_CMD"
    echo ""
}

# Function to check system requirements
check_system_requirements() {
    print_step "Checking system requirements..."
    
    # Check available memory
    if command -v free &> /dev/null; then
        TOTAL_MEM_GB=$(free -g | grep Mem | awk '{print $2}')
        if [ "$TOTAL_MEM_GB" -lt 4 ]; then
            print_warning "System has less than 4GB RAM. Performance tests may be limited."
        else
            print_status "Memory: ${TOTAL_MEM_GB}GB available"
        fi
    fi
    
    # Check CPU cores
    if command -v nproc &> /dev/null; then
        CPU_CORES=$(nproc)
        if [ "$CPU_CORES" -lt 4 ]; then
            print_warning "System has less than 4 CPU cores. Performance tests may be limited."
        else
            print_status "CPUs: $CPU_CORES cores available"
        fi
    fi
    
    # Check disk space
    AVAILABLE_SPACE=$(df . | awk 'NR==2{print $4}')
    AVAILABLE_SPACE_GB=$((AVAILABLE_SPACE / 1024 / 1024))
    if [ "$AVAILABLE_SPACE_GB" -lt 2 ]; then
        print_warning "Less than 2GB disk space available. May affect test results storage."
    else
        print_status "Disk space: ${AVAILABLE_SPACE_GB}GB available"
    fi
    
    echo ""
}

# Function to prepare environment
prepare_environment() {
    print_step "Preparing environment..."
    
    # Create results directory
    mkdir -p "$RESULTS_DIR"
    print_status "Created results directory: $RESULTS_DIR"
    
    # Check if required files exist
    if [ ! -f "$DOCKERFILE" ]; then
        print_error "Performance Dockerfile not found: $DOCKERFILE"
        exit 1
    fi
    
    if [ ! -f "$PERFORMANCE_COMPOSE_FILE" ]; then
        print_error "Performance compose file not found: $PERFORMANCE_COMPOSE_FILE"
        exit 1
    fi
    
    print_status "All required files are present"
    echo ""
}

# Function to build performance Docker image
build_performance_image() {
    print_step "Building performance testing Docker image..."
    print_status "This may take several minutes..."
    
    # Build the image using docker-compose
    $COMPOSE_CMD -f "$PERFORMANCE_COMPOSE_FILE" build fix-parser-performance
    
    if [ $? -eq 0 ]; then
        print_status "Performance Docker image built successfully"
    else
        print_error "Failed to build performance Docker image"
        exit 1
    fi
    
    echo ""
}

# Function to run performance tests
run_performance_tests() {
    print_step "Running performance tests..."
    
    # Stop and remove any existing containers
    print_status "Cleaning up existing containers..."
    $COMPOSE_CMD -f "$PERFORMANCE_COMPOSE_FILE" down --remove-orphans 2>/dev/null || true
    
    # Run the performance test container
    print_status "Starting performance test container..."
    print_status "Container: $CONTAINER_NAME"
    print_status "Results will be saved to: $RESULTS_DIR/"
    
    echo ""
    echo -e "${CYAN}=== Performance Test Execution Starting ===${NC}"
    echo ""
    
    # Run the container and capture exit code
    $COMPOSE_CMD -f "$PERFORMANCE_COMPOSE_FILE" up --abort-on-container-exit fix-parser-performance
    TEST_EXIT_CODE=$?
    
    echo ""
    if [ $TEST_EXIT_CODE -eq 0 ]; then
        print_status "Performance tests completed successfully!"
    else
        print_error "Performance tests failed with exit code: $TEST_EXIT_CODE"
        return 1
    fi
    
    echo ""
}

# Function to collect results
collect_results() {
    print_step "Collecting performance test results..."
    
    # Check if results directory has content
    if [ -d "$RESULTS_DIR" ] && [ "$(ls -A $RESULTS_DIR)" ]; then
        print_status "Performance test results collected:"
        ls -la "$RESULTS_DIR/"
        
        # Display key metrics if available
        if [ -f "$RESULTS_DIR/performance_test.log" ]; then
            echo ""
            print_status "Key Performance Metrics:"
            echo -e "${CYAN}===========================================${NC}"
            
            # Extract throughput information
            if grep -q "messages/second" "$RESULTS_DIR/performance_test.log"; then
                echo ""
                echo "Throughput Results:"
                grep -A 1 -B 1 "messages/second" "$RESULTS_DIR/performance_test.log" | head -10
            fi
            
            # Extract latency information
            if grep -q "latency" "$RESULTS_DIR/performance_test.log"; then
                echo ""
                echo "Latency Results:"
                grep -E "(Average latency|P50 latency|P95 latency|P99 latency)" "$RESULTS_DIR/performance_test.log" | head -10
            fi
            
            echo -e "${CYAN}===========================================${NC}"
        fi
        
        # Look for any generated performance report files
        PERF_REPORT=$(find "$RESULTS_DIR" -name "stream_fix_parser_performance_*.txt" | head -1)
        if [ -n "$PERF_REPORT" ] && [ -f "$PERF_REPORT" ]; then
            print_status "Detailed performance report: $(basename "$PERF_REPORT")"
        fi
    else
        print_warning "No performance test results found in $RESULTS_DIR/"
    fi
    
    echo ""
}

# Function to cleanup
cleanup() {
    print_step "Cleaning up..."
    
    # Stop all containers
    $COMPOSE_CMD -f "$PERFORMANCE_COMPOSE_FILE" down --remove-orphans 2>/dev/null || true
    
    print_status "Cleanup completed"
    echo ""
}

# Function to display summary
display_summary() {
    echo -e "${GREEN}=== Performance Test Summary ===${NC}"
    echo ""
    
    if [ -d "$RESULTS_DIR" ]; then
        echo "📁 Results Location: $RESULTS_DIR/"
        
        if [ "$(ls -A $RESULTS_DIR 2>/dev/null)" ]; then
            echo "📊 Generated Files:"
            ls -la "$RESULTS_DIR/" | grep -v "^total" | awk '{print "   " $9 " (" $5 " bytes)"}'
        else
            echo "⚠️  No result files found"
        fi
        echo ""
    fi
    
    echo "🐳 Docker Commands for Manual Analysis:"
    echo "   View logs: docker logs $CONTAINER_NAME"
    echo "   Interactive shell: $COMPOSE_CMD -f $PERFORMANCE_COMPOSE_FILE run --rm fix-parser-performance /bin/bash"
    echo ""
    
    echo "📈 Next Steps:"
    echo "   - Review the performance results in $RESULTS_DIR/"
    echo "   - Compare with baseline measurements"
    echo "   - Analyze bottlenecks and optimization opportunities"
    echo "   - Run tests with different configurations if needed"
    echo ""
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  --build-only    Build the Docker image only"
    echo "  --run-only      Run tests using existing image"
    echo "  --cleanup       Clean up containers and images"
    echo "  --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build and run performance tests"
    echo "  $0 --build-only       # Only build the Docker image"
    echo "  $0 --run-only         # Run tests with existing image"
    echo "  $0 --cleanup          # Clean up Docker resources"
}

# Function to cleanup Docker resources
cleanup_docker() {
    print_step "Cleaning up Docker resources..."
    
    # Stop and remove containers
    $COMPOSE_CMD -f "$PERFORMANCE_COMPOSE_FILE" down --remove-orphans --rmi local --volumes
    
    # Remove dangling images
    docker image prune -f
    
    print_status "Docker cleanup completed"
}

# Main execution function
main() {
    case "${1:-}" in
        --build-only)
            check_docker
            prepare_environment
            build_performance_image
            print_status "Build completed. Use '$0 --run-only' to run tests."
            ;;
        --run-only)
            check_docker
            prepare_environment
            if run_performance_tests; then
                collect_results
                display_summary
            else
                print_error "Performance tests failed!"
                exit 1
            fi
            ;;
        --cleanup)
            check_docker
            cleanup_docker
            ;;
        --help)
            show_usage
            ;;
        "")
            # Default: full deployment and test run
            trap cleanup EXIT
            
            check_docker
            check_system_requirements
            prepare_environment
            build_performance_image
            
            if run_performance_tests; then
                collect_results
                display_summary
                print_status "Performance testing deployment completed successfully!"
            else
                print_error "Performance testing deployment failed!"
                exit 1
            fi
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
}

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi 