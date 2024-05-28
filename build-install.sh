git submodule update --init --recursive
mv deps/libzmq/CMakeLists.txt deps/libzmq/CMakeLists.orig.txt
sed 's/build the tests" ON/build the tests" OFF/' deps/libzmq/CMakeLists.orig.txt > deps/libzmq/CMakeLists.txt
mkdir build
cd build/

cmake -G Ninja ..
ninja
ninja install
