# Multi-stage build for optimized Linux trading system
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libc6-dev \
    libpthread-stubs0-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Set work directory
WORKDIR /app

# Copy source code
COPY . .

# Build the application with Linux optimizations
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native" \
          -DBUILD_TESTS=ON \
          .. && \
    make -j$(nproc)

# Production stage
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    libc6 \
    libpthread-stubs0-dev \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN useradd -m -s /bin/bash trader

# Copy built application
COPY --from=builder /app/build/fix-gateway /usr/local/bin/
COPY --from=builder /app/build/tests/test_* /usr/local/bin/
COPY --from=builder /app/config/ /app/config/
COPY --from=builder /app/docs/ /app/docs/

# Set proper permissions
RUN chmod +x /usr/local/bin/*

# Switch to non-root user
USER trader
WORKDIR /app

# Expose FIX protocol port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD /usr/local/bin/fix-gateway --health-check || exit 1

# Default command
CMD ["/usr/local/bin/fix-gateway"] 