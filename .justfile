build_cmake:
    rm -rf build
    cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=On -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache -Dprotobuf_BUILD_TESTS=OFF
build:
    cmake --build build -- -j Simulator
build_all: build_cmake build
    echo "Build done"

build_release_cmake:
    rm -rf build_release
    cmake -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=On -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache -Dprotobuf_BUILD_TESTS=OFF
build_release:
    cmake --build build_release --  -j Simulator
build_release_all: build_release_cmake build_release
    echo "Build release done"

clean:
    rm -rf ./build
clean_release:
    rm -rf ./build_release

run_release:
    bash ./brun_release.sh
run:
    bash ./brun.sh
install:
    cargo install --path ./rust_part