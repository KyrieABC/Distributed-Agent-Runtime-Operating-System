# (Distri../cpp/)Run: 1. cmake -S . -B build -> Reads cpp/CMakeLists.txt and generates teh build system inside


# Require at least CMake 3.16
cmake_minimum_required(VERSION 3.16)

# Defines the project
# CXX: project uses c++
project(
    DAR  
    VERSION 0.1.0  
    LANGUAGES CXX
)

# Distributed Agent Runtime (DAR) currently targets c++17
# -> Matters because we use feature: std::optional, std::string_view, if constexpr
set(CMAKE_CXX_STANDARD 17)
# Tells compiler to not downgrade to an older c++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Ask for standard c++17 rather than compiler specific GNU extensions such as gnu++17
# So GCC will effectively compile with -std=c++17 instead of -std=gnu++17
set(CMAKE_CXX_EXTENSIONS OFF)

# Central part
# Creates a CMake target named: dar(contain all implementation .cpp file)
# -----------------------------------------------
# DAR library
# -----------------------------------------------

# On linux, unless specify otherwise, CMake normal produce a static library such as libdar.a (the tests link against that library)
add_library(
    dar 
    src/dar/common/id.cpp 
    src/dar/common/status.cpp 

    src/dar/core/resource.cpp 
    src/dar/core/lifecycle.cpp 
    src/dar/core/object_ref.cpp 
    src/dar/core/agent_spec.cpp 
    src/dar/core/task_spec.cpp 
    src/dar/core/execution.cpp 
)

# this is what makes: #include "dar/core/execution.h" work
# file: cpp/src/dar/core/execution.h 
# This tells compiler: include directory = cpp/src
# public: dar itself needs this include directory, and anything linking against dar should inherit it too
target_include_directories(
    dar 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Enable useful compiler warnings
# Catch unused variables, suspicious conversion 

if(MSVC)
    target_compile_options(
        dar 
        PRIVATE 
            /W4
    )
else()
    target_compile_options(
        dar 
        PRIVATE 
            -Wall 
            -Wextra 
            -Wpedantic
    )
endif()

# -----------------------------------------
# Tests
# -------------------------------------------

# Enable CMake's testing infra
# allow you to run: ctest ... instead of manually launching each test executable
include(CTest)

# Test dependencies and targets are only configured if testing is enabled
# Normally enabled by default after include(CTest)
if(BUILD_TESTING)
 
    # lets CMake download/build dependencies
    # Used later in FetchContent_Declare(), FetchContent_MakeAvailable()
    include(FetchContent)

    FetchContent_Declare(
        googletest 
        URL 
        https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    )

    # prevent GoogleTest from overriding the parent project's runtime
    # Library settings when building with MSVC
    set(
        gtest_force_shared_crt 
        ON 
        CACHE BOOL "" 
        FORCE
    )

    FetchContent_MakeAvailable(googletest)

    # take all test.cpp and create one executable named: dar_tests
    add_executable( 
        dar_tests 
        test/common/id_test.cpp 
        test/core/lifecycle_test.cpp 
        test/core/resource_test.cpp 
        test/core/execution_test.cpp
    )

    # then link DAR implementation with GoogleTest main()
    target_link_libraries( 
        dar_tests 
        PRIVATE 
            dar 
            GTest::gtest_main
    )

    include(GoogleTest)

    # Tells CMake/CTest to inspect GoogleTests and register individual test
    # Instead of CTest seeing dar_tests, it sees: IdTest.RandomIdRoundTripsThroughHex,...
    gtest_discover_tests(dar_tests)

endif()
