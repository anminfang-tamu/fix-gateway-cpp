# StreamFixParser Performance Testing Guide

This guide explains how to run comprehensive performance tests for the StreamFixParser using Docker on Linux platforms.

## 🎯 Performance Test Overview

The performance testing suite includes:

- **Single-threaded throughput testing** - Maximum messages per second
- **Multi-threaded performance** - Scalability across CPU cores
- **Partial message handling** - TCP fragmentation scenarios
- **Sustained load testing** - Long-term performance stability
- **Latency measurements** - P50, P95, P99 percentiles
- **Memory usage monitoring** - Peak and current memory consumption

## 📋 Prerequisites

### System Requirements

- **OS**: Linux (Ubuntu 20.04+ recommended)
- **CPU**: 4+ cores recommended for optimal testing
- **RAM**: 4GB+ available memory
- **Disk**: 2GB+ free space for results
- **Docker**: Version 20.0+ with Docker Compose

### Docker Installation

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install docker.io docker-compose

# Start Docker service
sudo systemctl start docker
sudo systemctl enable docker

# Add user to docker group (logout/login required)
sudo usermod -aG docker $USER
```

## 🚀 Quick Start

### Option 1: One-Command Deployment (Recommended)

```bash
# Build and run all performance tests
./deploy_performance_tests.sh
```

### Option 2: Step-by-Step Execution

```bash
# 1. Build the performance testing image
./deploy_performance_tests.sh --build-only

# 2. Run the performance tests
./deploy_performance_tests.sh --run-only

# 3. Clean up when done
./deploy_performance_tests.sh --cleanup
```

### Option 3: Manual Docker Commands

```bash
# Build the performance image
docker-compose -f docker-compose.performance.yml build fix-parser-performance

# Run performance tests
docker-compose -f docker-compose.performance.yml up fix-parser-performance

# View results
ls -la performance_results/
```

## 📊 Understanding Results

### Performance Metrics Explained

#### Throughput Metrics

- **Messages/second**: Number of FIX messages parsed per second
- **MB/second**: Data throughput in megabytes per second
- **Total messages**: Total number of messages processed

#### Latency Metrics

- **Average latency**: Mean parsing time per message (nanoseconds)
- **P50 latency**: 50th percentile (median) parsing time
- **P95 latency**: 95th percentile parsing time (most messages faster)
- **P99 latency**: 99th percentile parsing time (critical for HFT)
- **Min/Max latency**: Fastest and slowest parsing times

#### Memory Metrics

- **Current memory**: Active memory usage during testing
- **Peak memory**: Maximum memory consumption

### Expected Performance Ranges

| Test Type        | Throughput         | Latency (P95) | Notes                      |
| ---------------- | ------------------ | ------------- | -------------------------- |
| Single-threaded  | 50K-200K msgs/sec  | < 5,000 ns    | Depends on CPU             |
| Multi-threaded   | 100K-500K msgs/sec | < 10,000 ns   | Scales with cores          |
| Partial messages | 10K-50K msgs/sec   | < 15,000 ns   | TCP fragmentation overhead |
| Sustained load   | 80-90% of peak     | < 20,000 ns   | Long-term stability        |

_Performance varies based on hardware, message complexity, and system load_

## 📁 Result Files

After running tests, check the `performance_results/` directory:

```
performance_results/
├── performance_test.log                    # Complete test output
├── stream_fix_parser_performance_YYYYMMDD_HHMMSS.txt  # Detailed metrics
└── system_info.txt                         # System configuration
```

### Key Result Files

- **performance_test.log**: Complete test execution log with all metrics
- **stream*fix_parser_performance*\*.txt**: Structured performance report
- **system_info.txt**: Hardware and system configuration details

## 🔧 Configuration Options

### Docker Resource Limits

Edit `docker-compose.performance.yml` to adjust resources:

```yaml
deploy:
  resources:
    limits:
      cpus: "4.0" # Adjust CPU allocation
      memory: 4G # Adjust memory limit
    reservations:
      cpus: "4.0"
      memory: 2G
```

### Performance Test Parameters

Modify test parameters in `tests/test_stream_fix_parser_performance.cpp`:

```cpp
// Adjust test message counts
testSingleThreadedThroughput(100000);  // Number of messages
testMultiThreadedPerformance(4, 25000); // Threads, messages per thread
testSustainedLoad(std::chrono::seconds(30)); // Duration
```

## 🐛 Troubleshooting

### Common Issues

#### 1. "Docker daemon is not running"

```bash
sudo systemctl start docker
sudo systemctl status docker
```

#### 2. "Permission denied" when running Docker

```bash
sudo usermod -aG docker $USER
# Logout and login again
```

#### 3. Low performance results

- Ensure adequate CPU/memory resources
- Run with privileged mode for CPU governor control:
  ```bash
  docker-compose -f docker-compose.performance.yml up --privileged fix-parser-performance
  ```

#### 4. Build failures

```bash
# Clean Docker cache and rebuild
docker system prune -f
./deploy_performance_tests.sh --cleanup
./deploy_performance_tests.sh --build-only
```

#### 5. Out of memory during tests

- Reduce test message counts in source code
- Increase Docker memory limits
- Ensure sufficient system memory

### Performance Optimization Tips

#### For Maximum Performance

1. **Use privileged mode** for CPU governor control
2. **Pin CPU cores** using cpuset in docker-compose
3. **Disable swap** on the host system
4. **Use performance CPU governor**:
   ```bash
   sudo cpupower frequency-set -g performance
   ```

#### For Consistent Results

1. **Close unnecessary applications** during testing
2. **Run multiple test iterations** and average results
3. **Monitor system load** with `htop` during tests
4. **Use dedicated testing environment** when possible

## 📈 Benchmarking Guidelines

### Baseline Performance Testing

1. Run tests on a clean system
2. Record hardware specifications
3. Save results as baseline for comparison
4. Document system configuration

### Regression Testing

```bash
# Run performance tests after code changes
./deploy_performance_tests.sh

# Compare with baseline results
diff baseline_results/ performance_results/
```

### Production Readiness Assessment

- **Throughput**: Should meet or exceed target messages/second
- **Latency P99**: Should be below acceptable threshold (typically < 100μs for HFT)
- **Memory usage**: Should remain stable during sustained load
- **Error rate**: Should be zero under normal conditions

## 🔍 Advanced Analysis

### Using Docker for Profiling

```bash
# Run with performance monitoring
docker-compose -f docker-compose.performance.yml --profile monitoring up

# Access system metrics at http://localhost:9100
```

### Interactive Analysis

```bash
# Start container with shell access
docker-compose -f docker-compose.performance.yml run --rm fix-parser-performance /bin/bash

# Run individual components
/usr/local/bin/test_stream_fix_parser_performance
/usr/local/bin/test_stream_fix_parser
```

### Custom Test Scenarios

```bash
# Build and customize your own tests
docker build -f Dockerfile.performance -t custom-perf-test .
docker run -v $(pwd)/results:/app/performance_results custom-perf-test
```

## 📞 Support

If you encounter issues:

1. **Check logs**: `docker logs fix-parser-performance-test`
2. **Verify system requirements**: Run `./deploy_performance_tests.sh --help`
3. **Review container status**: `docker ps -a`
4. **Inspect resource usage**: `docker stats`

## 🎯 Performance Targets

### High-Frequency Trading Requirements

- **Latency P99**: < 10 microseconds
- **Throughput**: > 100K messages/second
- **Memory**: < 100MB steady state
- **Jitter**: < 1 microsecond variation

### Standard Financial Applications

- **Latency P99**: < 100 microseconds
- **Throughput**: > 10K messages/second
- **Memory**: < 500MB steady state
- **Availability**: 99.9% uptime

---

**Note**: Performance results are highly dependent on hardware specifications, system configuration, and concurrent system load. Use these tests to establish baselines and identify optimization opportunities rather than absolute performance guarantees.
