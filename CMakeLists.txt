cmake_minimum_required(VERSION 3.20)
project(DRoCM VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

# ============================================================
# Build configuration
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DROCM_BUILD_TESTS "Build unit tests" ON)
option(DROCM_BUILD_BENCHMARK "Build benchmark programs" ON)
option(DROCM_BUILD_EXAMPLES "Build example programs" ON)
option(DROCM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(DROCM_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(DROCM_PREFER_USR_LOCAL "Prefer packages installed under /usr/local" ON)

if(DROCM_ENABLE_ASAN AND DROCM_ENABLE_TSAN)
  message(FATAL_ERROR "DROCM_ENABLE_ASAN and DROCM_ENABLE_TSAN cannot be enabled together")
endif()

# ============================================================
# Dependencies
# ============================================================
if(DROCM_PREFER_USR_LOCAL)
  list(PREPEND CMAKE_PREFIX_PATH "/usr/local")
endif()

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(utf8_range CONFIG QUIET)
find_package(absl CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_program(PROTOC_EXECUTABLE NAMES protoc HINTS /usr/local/bin)

if(NOT PROTOC_EXECUTABLE)
  if(DEFINED Protobuf_PROTOC_EXECUTABLE)
    set(PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}")
  elseif(DEFINED PROTOBUF_PROTOC_EXECUTABLE)
    set(PROTOC_EXECUTABLE "${PROTOBUF_PROTOC_EXECUTABLE}")
  endif()
endif()

if(NOT PROTOC_EXECUTABLE)
  message(FATAL_ERROR "Unable to locate protoc executable")
endif()

set(GRPC_CPP_PLUGIN_EXECUTABLE "")
if(TARGET gRPC::grpc_cpp_plugin)
  get_target_property(GRPC_CPP_PLUGIN_EXECUTABLE gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
endif()
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  find_program(GRPC_CPP_PLUGIN_EXECUTABLE NAMES grpc_cpp_plugin HINTS /usr/local/bin)
endif()
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  message(FATAL_ERROR "Unable to locate grpc_cpp_plugin executable")
endif()

find_package(prometheus-cpp CONFIG QUIET)
if(prometheus-cpp_FOUND)
  message(STATUS "Found prometheus-cpp: ${prometheus-cpp_DIR}")
  add_compile_definitions(PROMETHEUS_CPP_PULL_FOUND=1)
else()
  message(STATUS "prometheus-cpp NOT found - metrics will be stubs")
endif()

# ============================================================
# Shared compile options
# ============================================================
add_library(drocm_build_options INTERFACE)
target_compile_features(drocm_build_options INTERFACE cxx_std_20)
target_compile_options(drocm_build_options INTERFACE
  -Wall
  -Wextra
  -Wpedantic
)

target_include_directories(drocm_build_options INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(drocm_build_options INTERFACE DEBUG)
endif()

if(DROCM_ENABLE_ASAN)
  target_compile_options(drocm_build_options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(drocm_build_options INTERFACE -fsanitize=address)
endif()

if(DROCM_ENABLE_TSAN)
  target_compile_options(drocm_build_options INTERFACE -fsanitize=thread)
  target_link_options(drocm_build_options INTERFACE -fsanitize=thread)
endif()

# ============================================================
# Protobuf & gRPC code generation
# ============================================================
set(PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(GENERATED_PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_PROTO_DIR}")

file(GLOB PROTO_FILES CONFIGURE_DEPENDS "${PROTO_ROOT}/*.proto")
if(PROTO_FILES STREQUAL "")
  message(FATAL_ERROR "No .proto files found under ${PROTO_ROOT}")
endif()

set(GENERATED_PROTO_SRCS "")
set(GENERATED_PROTO_HDRS "")

foreach(proto_file IN LISTS PROTO_FILES)
  get_filename_component(proto_name "${proto_file}" NAME_WE)

  set(pb_src "${GENERATED_PROTO_DIR}/${proto_name}.pb.cc")
  set(pb_hdr "${GENERATED_PROTO_DIR}/${proto_name}.pb.h")
  set(grpc_pb_src "${GENERATED_PROTO_DIR}/${proto_name}.grpc.pb.cc")
  set(grpc_pb_hdr "${GENERATED_PROTO_DIR}/${proto_name}.grpc.pb.h")

  add_custom_command(
    OUTPUT
      "${pb_src}"
      "${pb_hdr}"
      "${grpc_pb_src}"
      "${grpc_pb_hdr}"
    COMMAND ${PROTOC_EXECUTABLE}
      --proto_path=${PROTO_ROOT}
      --cpp_out=${GENERATED_PROTO_DIR}
      --grpc_out=${GENERATED_PROTO_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
      ${proto_file}
    DEPENDS
      "${proto_file}"
    COMMENT "Generating protobuf/gRPC sources for ${proto_name}.proto"
    VERBATIM
  )

  list(APPEND GENERATED_PROTO_SRCS "${pb_src}" "${grpc_pb_src}")
  list(APPEND GENERATED_PROTO_HDRS "${pb_hdr}" "${grpc_pb_hdr}")
endforeach()

add_library(drocm_proto_generated ${GENERATED_PROTO_SRCS} ${GENERATED_PROTO_HDRS})
target_link_libraries(drocm_proto_generated
  PUBLIC
    drocm_build_options
    protobuf::libprotobuf
    gRPC::grpc++
)

# Some static protobuf distributions need explicit log/utf8 components.
foreach(optional_dep IN ITEMS
  absl::absl_check
  absl::absl_log
  absl::log_initialize
  absl::log_internal_check_op
  absl::log_internal_message
  absl::log_internal_conditions
  absl::log_internal_format
  absl::raw_logging_internal
  absl::base
  utf8_range::utf8_range
  utf8_range::utf8_validity
)
  if(TARGET ${optional_dep})
    target_link_libraries(drocm_proto_generated PUBLIC ${optional_dep})
  endif()
endforeach()

# ============================================================
# Core library
# ============================================================
file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
file(GLOB_RECURSE REGISTRY_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/registry/*.cpp")
file(GLOB_RECURSE NODE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/node/*.cpp")
file(GLOB_RECURSE UTILS_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.cpp")

add_library(drocm_core
  ${CORE_SRC}
  ${REGISTRY_SRC}
  ${NODE_SRC}
  ${UTILS_SRC}
)

target_link_libraries(drocm_core
  PUBLIC
    drocm_build_options
    drocm_proto_generated
    spdlog::spdlog
    Threads::Threads
    absl::strings
    absl::absl_check
    absl::absl_log
    absl::log
    absl::time
    absl::synchronization
    absl::status
)

if(prometheus-cpp_FOUND)
  target_link_libraries(drocm_core PUBLIC prometheus-cpp::core prometheus-cpp::pull)
  target_compile_definitions(drocm_core PUBLIC DROCM_ENABLE_PROMETHEUS=1)
endif()

# ============================================================
# Main executable
# ============================================================
add_executable(drocm "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(drocm PRIVATE drocm_core)

# ============================================================
# Tests
# ============================================================
if(DROCM_BUILD_TESTS)
  enable_testing()

  file(GLOB_RECURSE TEST_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(TEST_SRC)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
      find_package(GTest REQUIRED)
    endif()

    add_executable(drocm_tests ${TEST_SRC})
    target_link_libraries(drocm_tests
      PRIVATE
        drocm_core
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME drocm_tests COMMAND drocm_tests)
  endif()
endif()

# ============================================================
# Benchmarks
# ============================================================
if(DROCM_BUILD_BENCHMARK)
  file(GLOB_RECURSE BENCH_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/*.cpp")
  if(BENCH_SRC)
    add_executable(drocm_benchmark ${BENCH_SRC})
    target_link_libraries(drocm_benchmark PRIVATE drocm_core)
  endif()
endif()

# ============================================================
# Examples
# ============================================================
if(DROCM_BUILD_EXAMPLES)
  file(GLOB_RECURSE EXAMPLE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
  foreach(example_src IN LISTS EXAMPLE_SRC)
    get_filename_component(example_name "${example_src}" NAME_WE)
    add_executable("${example_name}" "${example_src}")
    target_link_libraries("${example_name}" PRIVATE drocm_core)
  endforeach()
endif()

# ============================================================
# Install
# ============================================================
install(TARGETS drocm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS drocm_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/drocm"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
