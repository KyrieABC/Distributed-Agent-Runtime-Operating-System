# Builds C++ runtime -> libdar.a
# optionally builds Python extension -> _dar.so
# optionally builds C++ test -> dar_tests
# pyproject.toml -> root/CMakeLists.txt -> cpp/CmakeLists.txt

# Distributed Agent Runtime (DAR) - C++ language configuration

# start with this, rather than `cmake_minimum_required(...)`
# -> because root/CMakelists.txt owns the overall project
# request C++17
set(CMAKE_CXX_STANDARD 17)
# do not fall back to an older C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Perfer standard C++17 instead of compiler-specific language extensions
set(CMAKE_CXX_EXTENSIONS OFF)


# DAR native runtime library
# Create a static-library build target named dar
#[[
dar 
-> compilation
-> id.cpp(id.cpp.o), status.cpp(status.cpp.o), runtime.cpp(runtime.cpp.o),...
--> archive
--> libdar.a
]]
add_library(
    # CMake target name
    # not Python package nor dar._dar
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
    # dar target itself gets that include directory
    # targets linking against dar inherit it
    #[[
    future:
    target_link_libraries(_dar, PRIVATE(dar))
    -> allows _dar to consume the public usage requirements of dar
    ]]
    PUBLIC 
    # CMAKE_CURRENT_SOURCE_DIR: absolute path to repo/cpp
    # -> repo/cpp/src
    # This allows source code to write something like: #include "dar/runtime/runtime.h" instead of #include "../../../runtime/runtime.h"
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# Target dar requires C++17
# Becuase its PUBLIC: consumers of dar inherit that requirement where applicable
target_compile_features(
    dar 
    PUBLIC 
        cxx_std_17
)


# the static runtime library is linked ito the Python extension
#[[ dar is a static library(libdar.a), 
 but compiled code is put inside a shared Python extension: _dar.cpython-313-x86_64-linux-gpu.so
 ! On Linux system, code incorporated into shared object generally needs to be position-independent

C++ sources -(-fPIC)-> libdar.a -(linked into)-> _dar.so
 -> without PIC, linking a static archive into extension can fail on platgorms that requires it
]]
set_target_properties(
    dar 
    PROPERTIES 
        POSITION_INDEPENDENT_CODE ON
)


# Python extensions
# when python packaging occurs, pyproject.toml overrides it to ON
if(DAR_BUILD_PYTHON)
    # Use modern FindPython integration in pybind11
    # pybind11 needs information about Python installation against which _dar is being built
    set(PYBIND11_FINDPYTHON ON)

    # request pybind11:
    find_package(
        # Find pybind11's CMake package
        pybind11 
        # use pybind11's package configuration files
        CONFIG 
        # If pybind11 isn't available, configuration fails
        REQUIRED 
    )

    # Create _dar (another CMake target)
    # a python native extension
    #[[
    dar : C++ static runtime library (libdar.a)
    _dar: Python extension (_dar.cpython-313-x86_64-linux-gnu.so)
    ]]
    pybind11_add_module(
        _dar
        # PYBIND11_MODULE (exception registration)
        src/dar/bindings/module.cpp 
        # Python resources <--> C++ ResourceRequest
        src/dar/bindings/resource_bindings.cpp 
        # NativeExecutionRef
        src/dar/bindings/execution_bindings.cpp 
        # NativeRuntime + Python callback bridge
        src/dar/bindings/runtime_bindings.cpp 
    )

    target_include_directories(
        _dar 
        # _dar needs to include path to compile, but _dar doesn't advertise this requirement to downstream targets?
        PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # Python extensions depends on the native DAR runtime
    # Python _dar.so - (target_link_libraries()) -> libdar.a -> actual runtime
    # make `runtime = dar.init(...)` eventually reach C++ Runtime, TaskManager
    target_link_libraries(
        _dar 
        # _dar consumes dar: does not ned to propagate dar as an interface dependency to another C++ target
        PRIVATE 
            dar
    )

    # Install into the Python package 
    # tells CMake where _dar should go when project is installed
    # Destination: dar/ (as Python package is dar)
    #[[
    Ultimately, desired:
    site_packages/dar/
      -> __init__.py
      -> exceptions.py
      -> agent/
      -> runtime/
      -> -dar.cpython-313-x86_64-linux-gnu.so
    so python could execute:
      1. from .. import _dar
      2. import dar._dar
    ]]
    install( 
        TARGETS _dar 
        LIBRARY DESTINATION dar 
        RUNTIME DESTINATION dar 
    )

endif()


# C++ tests 
if(DAR_BUILD_TESTS)
    if(BUILD_TESTING)
        # Loads CMake's FetchContent functionality
        include(FetchContent)

        # declares an external dependency called: `googletest`
        # FetchContent_Declare describes dependency (FetchContent_MakeAvailable() make it available to the build)
        FetchContent_Declare(
            googletest 
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        )
    
        # GoogleTest CRT option
        set(
            gtest_force_shared_crt
            ON 
            # stores this as a Boolean cache variable
            CACHE BOOL 
            ""
            # overwrites an existign cached value
            FORCE 
        )
 
        # Make GoogleTest available
        # GoogleTest contributes targets such as: Gtest::gtest, GTest::gtest_main
        FetchContent_MakeAvailable(googletest)

        # Creates an executable target: dar_tests
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

        # gives dar_tests two dependencies:
        # 1. dar (runtime being tested)
        # 2. GTest::gtest_main (provides GoogleTest functionality and a default main())
        # C++ tests and Python binding both consume the same native dar target
        target_link_libraries(
            dar_tests
            PRIVATE 
                dar 
                GTest::gtest_main
        )

        # load CMake's GoogleTest integration
        include(GoogleTest)
        # asks CMake to inspect the GoogleTest executable and register individual GoogleTest cases with CTest
        # why `ctest --test-dir build` can report individual testsinstead of treating the entire executable as one opaque test
        gtest_discover_tests(dar_tests)
    endif()
endif()
    
        

