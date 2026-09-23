# =====================================
# Distributed Agent Runtime (DAR)
# =====================================

# Configure from cpp/ with:
# `cmake -S . -B build`
# Build with:
# `cmake --build build`
# Run C++ tests with:
# `ctest --test-dir build --output-on-failure`
# Later, when Python binding source files exist
# `cmake -S . -B build -DDAR_BUILD_PYTHON=ON`
# `cmake --build build`

# ---------------------------------------
# Project COnfiguration
# ----------------------------------------

cmake_minimum_required(VERSION 3.16)

project(
    DAR 
    VERSION 0.2.0 
    LANGUAGES CXX
)

# DAR currently targets C++ 17
# C++ 17 is required for features already used by runtime,
# including std::optional and std::stirng_view
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use standard C++17 instead of compiler-specific extensions such as GNU++17
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------
# Build options
# ---------------------------

# Phase 2 introduces optional Python SDK/native extension
# Keep this OFF by default so existing C++ runtime can still be configured and built without Python or pybind11 installed
option(
    DAR_BUILD_PYTHON 
    "Build the DAR Python extension module" 
    OFF
)

