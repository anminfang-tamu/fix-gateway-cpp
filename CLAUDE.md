# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Core Development Philosophy

**"Simple is Best, Reliable is Best, Maintainable is Best, Readable is Best"**

Following the Unix philosophy and principles of robust system design:

### Fundamental Principles
1. **Testing First**: Every feature starts with tests. Code without tests is broken code.
2. **Simplicity**: Choose the simplest solution that works. Complexity is the enemy of reliability.
3. **Reliability**: Systems must fail gracefully and recover automatically.
4. **Maintainability**: Code should be obvious to read and modify years later.
5. **Readability**: Code is written once but read hundreds of times.

### Development Rules
- **Test-Driven Development**: Write tests before implementation
- **Single Responsibility**: Each function/class does one thing well
- **Clear Naming**: Names should explain intent without comments
- **Error Handling**: Every error path must be tested and handled
- **Documentation**: Code should be self-documenting; comments explain "why", not "what"
- **Performance**: Measure, don't guess. Optimize only after profiling.

### Testing Strategy
- Unit tests for all business logic
- Integration tests for component interactions  
- Performance tests for latency-critical paths
- Stress tests for system limits
- All tests must be deterministic and fast

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

### Message Flow Design
**Critical Principle**: Strict separation between message processing and network I/O

```
[Network] → [Inbound Queue] → [InboundMessageManager] → [Outbound Priority Queues] → [AsyncSender Threads] → [Network]
```

**Core Components**:
- `StreamFixParser`: Parses incoming FIX messages from network
- `LockfreeQueue`: Sub-microsecond latency inbound queue (84ns-1000ns)
- `InboundMessageManager`: Processes session/business logic, routes to outbound queues
  - `FixSessionManager`: Handles session-level messages (Logon, Logout, Heartbeat, etc.)
  - `BusinessLogicManager`: Handles trading messages (NewOrder, Cancel, etc.)
- `PriorityQueueContainer`: Outbound queues by priority (Critical, High, Medium, Low)
- `MessageRouter`: Routes processed messages to appropriate priority queues
- `AsyncSender`: Separate threads monitoring outbound queues, handling TCP transmission

### Queue Architecture
- **Inbound Queue**: Single lock-free queue for all incoming parsed messages
- **Outbound Priority Queues**: 
  - `CRITICAL`: Session-critical messages (Logon, Logout) - 2048 capacity
  - `HIGH`: Time-sensitive trading messages (ExecutionReport) - 2048 capacity  
  - `MEDIUM`: Standard trading messages (NewOrder) - 1024 capacity
  - `LOW`: Administrative messages (Status requests) - 512 capacity

### Thread Separation
- **Processing Threads**: InboundMessageManagers process messages and route to outbound queues
- **Network Threads**: AsyncSender threads independently monitor outbound queues and handle TCP
- **No Direct TCP**: Message managers NEVER directly handle network connections

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