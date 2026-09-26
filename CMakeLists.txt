#[[

rm -rf build
cmake -S . -B build
-> `.` tells CMake: Start by reading the CMakelists.txt in repository root
cmake --build build -j
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
]]

# Configure the whoe repository and enters C++ project
# pyproject.toml -> root/CMakeLists.txt -> cpp/CmakeLists.txt

# DAR requires CMake 3.16 or newer
# Cmake executable (check if version >= 3.16) if yes-> continue configuration
cmake_minimum_required(VERSION 3.16)

# Project declaration (name: DAR)
#[[
project(name, version, DAR_VERSION)
same version as in pyproject.toml
`LANGUAGES CXX`: the project needs a C++ compiler
]]
project(
    DAR 
    VERSION 0.2.0
    LANGUAGES CXX
)

# Repository-wide build options
# Python packaging will override these through scikit-build-core
#[[
Creates a boolean CMake variable: DAR_BUILD_PYTHON 
-> default(OFF)
-> Native development: cmake -S . -B build (means DAR_BUILD_PYTHON = OFF)
(coud be overrided by) `cmake -S . -B build -DDAR_BUILD_PYTHON=ON`
]]
option(
    DAR_BUILD_PYTHON 
    "Build the DAR python extension module"
    OFF
)

option(
    DAR_BUILD_TESTS 
    "Build DAR C++ tests"
    ON
)

# Enable CTest at the top-level project
# Only configure CMake's testing infrastructure when DAR's C++ tests were requested
if(DAR_BUILD_TESTS)
    # provides CTest integration and normally establishes: `BUILD_TESTING=ON` unless overriden
#[[
Later(cpp/CMakeLists.txt): if(DAR_BUILD_TESTS): if(BUILD_TESTING)
-> DAR_BUILD_TEST?(if yes)-> include(CTest) -> BUILD_TESTING(if yes) -> actually create dar_tests
-> Also enable top-level test discovery?
--> why `ctest --test-dir build` can see tests registered from the cpp subdirectory 
]]
    include(CTest)
endif()

# bridge between 2 CMake files 
# Tells CMake: Enter cpp/ and process cpp/CMakeLists.txt as part of this same build config
# Also why in cpp/CMakelists.txt: simply write `if(DAR_BUILD_PYTHON)` without redefining
add_subdirectory(cpp)