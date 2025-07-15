#!/bin/bash

# FIX Gateway Linux Deployment Script
# This script deploys the trading system on Linux with optimal thread pinning

set -e

echo "🚀 FIX Gateway Linux Deployment Script"
echo "======================================="

# Check if running as root for some operations
if [[ $EUID -eq 0 ]]; then
    echo "⚠️  Running as root - full thread pinning capabilities available"
    ROOT_PRIVILEGES=true
else
    echo "ℹ️  Running as non-root user - some optimizations may be limited"
    ROOT_PRIVILEGES=false
fi

# System information
echo "📊 System Information:"
echo "   OS: $(uname -o)"
echo "   Kernel: $(uname -r)"
echo "   CPU cores: $(nproc)"
echo "   Available memory: $(free -h | awk '/^Mem:/ {print $2}')"

# Check Docker installation
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed. Please install Docker first."
    exit 1
fi

if ! command -v docker-compose &> /dev/null; then
    echo "❌ Docker Compose is not installed. Please install Docker Compose first."
    exit 1
fi

echo "✅ Docker and Docker Compose are installed"

# Check CPU isolation (optional but recommended for trading)
echo "🔍 Checking CPU isolation..."
if grep -q "isolcpus" /proc/cmdline; then
    echo "✅ CPU isolation is configured:"
    cat /proc/cmdline | grep -o "isolcpus=[^ ]*"
else
    echo "⚠️  CPU isolation not configured. For optimal performance, consider:"
    echo "   Adding 'isolcpus=0-3 nohz_full=0-3 rcu_nocbs=0-3' to kernel boot parameters"
fi

# Check if hugepages are available
echo "🔍 Checking hugepages..."
if [ -d /sys/kernel/mm/hugepages ]; then
    echo "✅ Hugepages are available"
    echo "   Current configuration:"
    for dir in /sys/kernel/mm/hugepages/hugepages-*; do
        size=$(basename "$dir" | sed 's/hugepages-//')
        nr_hugepages=$(cat "$dir/nr_hugepages" 2>/dev/null || echo "0")
        echo "   $size: $nr_hugepages pages"
    done
else
    echo "⚠️  Hugepages not available"
fi

# Create necessary directories
echo "📁 Creating directories..."
mkdir -p logs
mkdir -p config
mkdir -p monitoring

# Create monitoring configuration
echo "📈 Creating monitoring configuration..."
cat > monitoring/prometheus.yml << EOF
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'fix-gateway'
    static_configs:
      - targets: ['fix-gateway:8081']
    scrape_interval: 5s
    metrics_path: /metrics
    
  - job_name: 'redis'
    static_configs:
      - targets: ['message-broker:6379']
    scrape_interval: 10s
EOF

# Build and deploy
echo "🏗️  Building and deploying..."

# Pull base images
echo "📥 Pulling base images..."
docker-compose pull monitoring message-broker

# Build the trading application
echo "🔨 Building trading application..."
docker-compose build fix-gateway

# Deploy the stack
echo "🚀 Starting services..."
docker-compose up -d

# Wait for services to be ready
echo "⏳ Waiting for services to be ready..."
sleep 10

# Check service status
echo "📊 Service Status:"
docker-compose ps

# Check if fix-gateway is healthy
echo "🏥 Health Checks:"
for i in {1..30}; do
    if docker-compose exec -T fix-gateway /usr/local/bin/fix-gateway --health-check &>/dev/null; then
        echo "✅ FIX Gateway is healthy"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "❌ FIX Gateway health check failed"
        exit 1
    fi
    echo "   Attempt $i/30: Waiting for FIX Gateway to be ready..."
    sleep 2
done

# Show thread affinity information
echo "🧵 Thread Affinity Information:"
docker-compose exec fix-gateway sh -c "
    echo 'Container CPU affinity:'
    cat /proc/self/status | grep Cpus_allowed_list
    echo 'Available CPU cores:'
    cat /proc/cpuinfo | grep 'processor' | wc -l
"

# Performance testing
echo "🔬 Running performance tests..."
docker-compose run --rm load-tester

# Show logs
echo "📋 Recent logs:"
docker-compose logs --tail=20 fix-gateway

# Show monitoring URLs
echo "🌐 Monitoring URLs:"
echo "   Prometheus: http://localhost:9090"
echo "   Fix Gateway: http://localhost:8080"
echo "   Redis: redis://localhost:6379"

# Performance tuning suggestions
echo "⚡ Performance Tuning Suggestions:"
echo "   1. For production, consider CPU isolation:"
echo "      Add 'isolcpus=0-3 nohz_full=0-3 rcu_nocbs=0-3' to /etc/default/grub"
echo "   2. Increase system limits:"
echo "      echo 'trader soft rtprio 99' >> /etc/security/limits.conf"
echo "      echo 'trader hard rtprio 99' >> /etc/security/limits.conf"
echo "   3. Enable hugepages:"
echo "      echo 'vm.nr_hugepages = 1024' >> /etc/sysctl.conf"
echo "   4. Disable CPU frequency scaling:"
echo "      echo performance > /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"

# Cleanup function
cleanup() {
    echo "🧹 Cleaning up..."
    docker-compose down
}

# Set up signal handlers
trap cleanup EXIT

echo "✅ Deployment complete!"
echo "🎯 The FIX Gateway is now running with Linux thread pinning capabilities"
echo "📊 Monitor performance at: http://localhost:9090"
echo "🛑 To stop: docker-compose down" 