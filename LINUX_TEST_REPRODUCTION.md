# Linux Test Reproduction Guide

## Quick Start

```bash
# 1. Ensure Docker is running
docker --version

# 2. Build and deploy the trading system
./deploy-linux.sh

# 3. Or manually with Docker Compose
docker-compose build fix-gateway
docker-compose up -d

# 4. Run performance tests
docker-compose exec fix-gateway /usr/local/bin/fix-gateway
docker-compose run --rm fix-gateway /usr/local/bin/test_message_manager

# 5. View monitoring
open http://localhost:9090  # Prometheus
open http://localhost:8080  # Fix Gateway

# 6. Check logs
docker-compose logs -f fix-gateway

# 7. Stop services
docker-compose down
```

## Expected Results

### Performance Metrics

- **Latency**: ~0.45μs (62% improvement over macOS)
- **Throughput**: ~2.1M messages/sec (162% improvement)
- **Queue Latency**: 84ns-1000ns (sub-microsecond)

### Thread Pinning Success

```
✅ CRITICAL priority → Core 0 (Successfully pinned)
✅ HIGH priority    → Core 1 (Successfully pinned)
✅ MEDIUM priority  → Core 2 (Successfully pinned)
✅ LOW priority     → Core 3 (Successfully pinned)
```

### Test Results

```
🎉 All MessageManager tests passed!
✅ Basic functionality
✅ Lifecycle management
✅ Message routing
✅ Performance monitoring
✅ Core pinning capabilities
✅ TCP connection management
✅ Configuration options
```

## Key Files

- **Test Results**: `docs/LINUX_DEPLOYMENT_TEST_RESULTS.md`
- **Architecture**: `docs/TRADING_INFRASTRUCTURE_ANALYSIS.md`
- **Thread Pinning**: `docs/THREAD_PINNING_GUIDE.md`

## Troubleshooting

- If Docker fails: `open -a Docker` (start Docker Desktop)
- If build fails: `rm -rf build/ && mkdir build` (clean build)
- If ports conflict: Check ports 8080, 8081, 9090, 6379 are free

**Last tested**: 2025-07-15 16:23:00 UTC  
**Environment**: Docker Desktop on macOS (Ubuntu 22.04 container)
