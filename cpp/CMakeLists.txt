#[[
# rm -rf build

cmake -S . -B build \
    -DDAR_BUILD_PYTHON=OFF \
    -DDAR_BUILD_TESTS=ON

cmake --build build -j

ctest --test-dir build -N

ctest --test-dir build --output-on-failure
]]

# Distributed Agent Runtime (DAR)

cmake_minimum_required(VERSION 3.16)

project(
    DAR 
    VERSION 0.2.0
    LANGUAGES CXX
)


# C++ Configuration

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Build Options

option(
    DAR_BUILD_PYTHON
    "Build the DAR python extension module"
    OFF
)

option(
    DAR_BUILD_TESTS
    "Build DAR C++ test"
    ON
)


# DAR native runtime library

add_library(
    dar
    STATIC 

    # Common
    src/dar/common/id.cpp 
    src/dar/common/status.cpp 

    # Core
    src/dar/core/agent_spec.cpp 
    src/dar/core/execution.cpp 
    src/dar/core/lifecycle.cpp 
    src/dar/core/object_ref.cpp 
    src/dar/core/resource.cpp 
    src/dar/core/task_spec.cpp 

    # Runtime
    src/dar/runtime/runtime.cpp 
    src/dar/runtime/runtime_context.cpp 
    src/dar/runtime/task_manager.cpp 

    # Scheduler
    src/dar/scheduler/placement.cpp 
    src/dar/scheduler/resource_manager.cpp 
    src/dar/scheduler/scheduler.cpp 
    src/dar/scheduler/task_queue.cpp 

    # Worker 
    src/dar/worker/executor.cpp 
    src/dar/worker/worker.cpp 
    src/dar/worker/worker_pool.cpp 
)

target_include_directories(
    dar 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(
    dar 
    PUBLIC 
        cxx_std_17
)


# the static runtime library is linked ito the Python extension
# Position-independent code is required on platforms where a static library is linked into a shared/module library
set_target_properties(
    dar 
    PROPERTIES 
        POSITION_INDEPENDENT_CODE ON
)


# Python extensions

if(DAR_BUILD_PYTHON)
    # Use modern FindPython integration in pybind11
    set(PYBIND11_FINDPYTHON ON)

    find_package(
        pybind11 
        CONFIG 
        REQUIRED 
    )

    pybind11_add_module(
        _dar
        src/dar/bindings/module.cpp 
        src/dar/bindings/resource_bindings.cpp 
        src/dar/bindings/execution_bindings.cpp 
        src/dar/bindings/runtime_bindings.cpp 
    )

    target_include_directories(
        _dar 
        PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(
        _dar 
        PRIVATE 
            dar
    )

    # Install into the Python package 
    # site-packages / dar / _dar.<platform-extension>
    install( 
        TARGETS _dar 
        LIBRARY DESTINATION dar 
        RUNTIME DESTINATION dar 
    )

endif()


# C++ tests 
if(DAR_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        include(FetchContent)

        FetchContent_Declare(
            googletest 
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        )

        set(
            gtest_force_shared_crt
            ON 
            CACHE BOOL 
            ""
            FORCE
        )

        FetchContent_MakeAvailable(googletest)

        add_executable(
            dar_tests 

            test/common/id_test.cpp 
            test/common/status_test.cpp 

            test/core/execution_test.cpp 
            test/core/lifecycle_test.cpp 
            test/core/resource_test.cpp 

            test/worker/executor_test.cpp 
            test/phase1/runtime_stress_test.cpp 
        )

        target_link_libraries(
            dar_tests 
            PRIVATE 
                dar 
                GTest::gtest_main 
        )

        include(GoogleTest)
        gtest_discover_tests(dar_tests)

    endif()

endif()
    
        

