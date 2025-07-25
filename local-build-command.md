# Clean rebuild

cd /Users/anminfang/fix-gateway-cpp
rm -rf build
mkdir build && cd build

# Configure and build

cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON ..
make -j$(sysctl -n hw.ncpu)

# Run core tests

./tests/test_message_manager
./fix-demo
./quick-perf-demo
