#[[
rm -rf build
cmake -S . -B build
cmake --build build --target dar_phase3_proto -j
]]
#[[
# ============================================================
# 1. CLEAN
# ============================================================

rm -rf build


# ============================================================
# 2. CONFIGURE
# ============================================================

cmake -S . -B build


# ============================================================
# 3. BUILD ONLY THE PHASE-3 PROTO TARGET FIRST
#    Verifies:
#    .proto -> protoc -> generated C++ -> dar_phase3_proto
# ============================================================

cmake --build build --target dar_phase3_proto -j


# ============================================================
# 4. VERIFY GENERATED PROTOBUF/gRPC FILES
# ============================================================

ls build/proto/generated


# ============================================================
# 5. BUILD THE PHASE-3 TRANSPORT LIBRARY
#    Verifies:
#    serialization
#    ConnectionPool
#    RpcClient
#    RpcServer
# ============================================================

cmake --build build --target dar_phase3_transport -j


# ============================================================
# 6. BUILD THE TEST EXECUTABLE
#    This is the step you were missing when CTest reported:
#    dar_tests_NOT_BUILT
# ============================================================

cmake --build build --target dar_tests -j


# ============================================================
# 7. SHOW ALL TESTS REGISTERED WITH CTEST
# ============================================================

ctest --test-dir build -N


# ============================================================
# 8. RUN ALL TESTS
# ============================================================

ctest --test-dir build --output-on-failure
]]


# DAR Phase 3 - Protobuf / gRPC wire contracts
#[[Root CMakeLists.txt owns:
  - cmake_minimum_required(...)
  - project(...)
  - repository-wide options(...)
  - CTest initialization
]]

# Dependencies
find_package(Protobuf REQUIRED)

# System installation exposes gRPC through pkg-config rather than gRPCConfig.cmake
find_package(PkgConfig REQUIRED)

pkg_check_modules(
    GRPCPP 
    REQUIRED 
    IMPORTED_TARGET 
    grpc++
)

# Locate the gRPC C++ protoc plugin used during code generation
find_program(
    GRPC_CPP_PLUGIN 
    NAMES grpc_cpp_plugin
    REQUIRED
)

# Directories
# This CMakeLists.txt lives in DAR/proto
set(DAR_PROTO_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Generated C++ files go into: DAR/build/proto/generated
set(DAR_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated" 
)

# create the generated folder inside build tree `DAR/build/proto/generated`
file(MAKE_DIRECTORY "${DAR_GENERATED_DIR}")

# Protocol Definition
set(DAR_PROTO_FILES
    common.proto 
    task.proto 
    node.proto 
    worker.proto 
    control.proto 
    object.proto  
)

# ============================================================
# Generated files
# ============================================================

# Create empty lists for generated files
# Each .proto file can generate 2 categories of C++ code (common.proto -> common.pb.cc/common.pb.h)
# DAR_PROTO_SRCS: stores *.pb.cc
set(DAR_PROTO_SRCS)
# DAR_PROTO_HDRS: stores *.pb.h 
set(DAR_PROTO_HDRS)
# For fils containing services, this is where generated service comes from.  then -> generated gRPC C++ interfaces
# DAR_GRPC_SRCS: stores *.grpc.pb.cc
set(DAR_GRPC_SRCS)
# DAR_GRPC_HDRS: stores *.grpc.pb.h
set(DAR_GRPC_HDRS)

# Loop through every .proto
foreach(proto ${DAR_PROTO_FILES})
    # Extract filename without .proto
    # NAME_WE: filename without extension :: common.h -> stem = "common"
    get_filename_component(stem "${proto}" NAME_WE)

    # DAR/build/proto/generated/${stem}.pb.cc
    list(APPEND DAR_PROTO_SRCS
        "${DAR_GENERATED_DIR}/${stem}.pb.cc"
    )
    
    # DAR/build/proto/generated/${stem}.pb.h
    list(APPEND DAR_PROTO_HDRS
        "${DAR_GENERATED_DIR}/${stem}.pb.h"
    )

    # DAR/build/proto/generated/${stem}.grpc.pb.cc
    list(APPEND DAR_GRPC_SRCS
        "${DAR_GENERATED_DIR}/${stem}.grpc.pb.cc"
    )

    # DAR/build/proto/generated/${stem}.grpc.pb.h
    list(APPEND DAR_GRPC_HDRS
        "${DAR_GENERATED_DIR}/${stem}.grpc.pb.h"
    )
endforeach()


# ============================================================
# Generate Protobuf + gRPC C++ sources
# ============================================================

# Actually generate the files
# command that knows how to create files
add_custom_command(
    # the custom command (protoc) produces all the files
    OUTPUT
        ${DAR_PROTO_SRCS}
        ${DAR_PROTO_HDRS}
        ${DAR_GRPC_SRCS}
        ${DAR_GRPC_HDRS}

    # protoc is Protocol Buffers compiler
    # common.proto -(command: protoc)-> *.pb.cc(*.pb.h, ...)
    # Use protobuf::protoc instead of /usr/bin/protoc because `find_package(Protobuf CONFIG REQUIRED)`
    COMMAND protobuf::protoc

    # argument to pass to protoc command
    ARGS
        # where .proto imports live (DAR_PROTO_DIR: DAR/repo)
        # For: `import "common.proto" (find it inside DAR_PROTO_DIR)
        --proto_path=${DAR_PROTO_DIR}
        # Protobuf's own include path
        --proto_path=${Protobuf_INCLUDE_DIRS}

        # Generate C++ message code and place it here (DAR_GENERATED_DIR)
        --cpp_out=${DAR_GENERATED_DIR}
        --grpc_out=${DAR_GENERATED_DIR}

        # Tells protoc which gRPC plugin to use
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}

        # Actual input files
        ${DAR_PROTO_FILES}

    # Run the protoc command as if the current directory is DAR/proto
    # As: `cd DAR/proto`, `protoc ... common.proto task.proto ...`
    WORKING_DIRECTORY
        ${DAR_PROTO_DIR}

    # Generated files depend upon the source .proto files
    # after `cmake --build build`, then change worker.proto, CMake could recognize: worker.proto changed -> generated output is now stable -> run protoc again
    DEPENDS
        ${DAR_PROTO_FILES}

    VERBATIM
)
# Now Cmake understand:
#[[
*.proto files -> protoc -> *.pb. files and *.grpc.pb. files
]]

# ============================================================
# Generated protocol library
# ============================================================

# 
add_library(
    dar_phase3_proto
    STATIC
        ${DAR_PROTO_SRCS}
        ${DAR_GRPC_SRCS}
)


# Generated headers:
#
#   common.pb.h
#   task.pb.h
#   worker.grpc.pb.h
#   ...
#
# become available to targets that link against
# dar_phase3_proto.
target_include_directories(
    dar_phase3_proto
    PUBLIC
        "${DAR_GENERATED_DIR}"
)


# Explicitly declare the language requirement for this target.
target_compile_features(
    dar_phase3_proto
    PUBLIC
        cxx_std_17
)

target_link_libraries(
    dar_phase3_proto
    PUBLIC
        protobuf::libprotobuf
        PkgConfig::GRPCPP
)

