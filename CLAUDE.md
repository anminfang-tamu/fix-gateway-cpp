# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

High-performance, low-latency FIX gateway trading system built in C++ with Linux optimization and Docker deployment.

## Build Commands

```bash
# Local build
mkdir build && cd build
cmake ..
make

# Docker deployment (recommended)
./deploy-linux.sh

# Manual Docker build
docker-compose build fix-gateway
docker-compose up -d

# Performance testing
docker-compose run --rm fix-gateway /usr/local/bin/test_message_manager
./run_performance_tests.sh
```

## Test Commands

```bash
# Run all tests
cd build
ctest

# Individual test executables
./test_checksum
./test_debug  
./test_length

# Performance benchmarks
./demos/quick_perf_demo
./demos/memory_performance_test
```

## Architecture

**Core Components**:
- `MessageManager`: Core-pinned threads with priority queues
- `AsyncSender`: Non-blocking message transmission  
- `StreamFixParser`: FIX protocol message parsing
- `LockfreeQueue`: Sub-microsecond latency data structures
- `PriorityQueue`: Message routing with priority handling

**Key Directories**:
- `include/` - Header files organized by module (application, common, manager, network, protocol, utils)
- `src/` - Implementation files matching include structure
- `demos/` - Performance testing and usage examples
- `tests/` - Unit tests with Google Test framework
- `docs/` - Architecture documentation and performance analysis

## Performance Focus

- **Target Latency**: Sub-microsecond message processing
- **Linux Optimization**: Thread pinning with `pthread_setaffinity_np`
- **Lock-free Design**: Priority queues and message pools
- **Memory Management**: Templated message pools for zero-copy operations
- **Monitoring**: Prometheus metrics integration

## Development Workflow

1. Use Docker for consistent Linux performance testing
2. Run performance benchmarks after changes: `./run_performance_tests.sh`
3. Check logs in `logs/` directory for debugging
4. Use `docs/` files for architectural understanding
5. Test on Linux containers for production-realistic results

## Key Performance Metrics

- Queue latency: 84ns-1000ns (sub-microsecond)
- Message throughput: 2.1M messages/sec
- End-to-end latency: 0.45μs on Linux
- Thread pinning: Full Linux support enabled