cmake_minimum_required(VERSION 3.25)

project(jubilant-db VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

option(JUBILANT_BUILD_TESTS "Build Jubilant DB tests" ON)
option(JUBILANT_ENABLE_CLANG_TIDY "Enable clang-tidy diagnostics" OFF)

# Third-party build options ----------------------------------------------------
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Dependencies -----------------------------------------------------------------
FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG v24.3.25
)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(flatbuffers googletest)

# Tooling ----------------------------------------------------------------------
if(JUBILANT_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
  if(CLANG_TIDY_EXECUTABLE)
    set(CMAKE_CXX_CLANG_TIDY
        ${CLANG_TIDY_EXECUTABLE}
        --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
    )
  else()
    message(WARNING "clang-tidy requested but not found on PATH")
  endif()
endif()

# FlatBuffers schema generation ------------------------------------------------
set(JUBILANT_SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schemas)
set(JUBILANT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${JUBILANT_GENERATED_DIR})

set(JUBILANT_SCHEMA_FILES
    ${JUBILANT_SCHEMA_DIR}/disk.fbs
    ${JUBILANT_SCHEMA_DIR}/wal.fbs
    ${JUBILANT_SCHEMA_DIR}/wire.fbs
)

set(JUBILANT_GENERATED_HEADERS)
foreach(schema ${JUBILANT_SCHEMA_FILES})
  get_filename_component(schema_name ${schema} NAME_WE)
  list(APPEND JUBILANT_GENERATED_HEADERS
       ${JUBILANT_GENERATED_DIR}/${schema_name}_generated.h)
endforeach()

add_custom_command(
  OUTPUT ${JUBILANT_GENERATED_HEADERS}
  COMMAND $<TARGET_FILE:flatc>
          --cpp
          --scoped-enums
          --gen-object-api
          -o ${JUBILANT_GENERATED_DIR}
          ${JUBILANT_SCHEMA_FILES}
  DEPENDS ${JUBILANT_SCHEMA_FILES} flatc
  COMMENT "Generating FlatBuffers C++ headers"
  VERBATIM
)

add_custom_target(generate_schemas ALL DEPENDS ${JUBILANT_GENERATED_HEADERS})

# Core library -----------------------------------------------------------------
add_library(jubildb
  src/config/config.cpp
  src/lock/lock_manager.cpp
  src/meta/manifest.cpp
  src/meta/superblock.cpp
  src/server/server.cpp
  src/storage/btree/btree.cpp
  src/storage/checksum.cpp
  src/storage/checkpoint/checkpointer.cpp
  src/storage/pager/pager.cpp
  src/storage/simple_store.cpp
  src/storage/vlog/value_log.cpp
  src/storage/wal/wal_manager.cpp
  src/txn/transaction_context.cpp
)

target_include_directories(jubildb
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${JUBILANT_GENERATED_DIR}
)

target_link_libraries(jubildb
  PUBLIC
    flatbuffers
)

add_dependencies(jubildb generate_schemas)

target_compile_options(jubildb
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# CLI --------------------------------------------------------------------------
add_executable(jubectl
  tools/jubectl/main.cpp
)

target_link_libraries(jubectl
  PRIVATE
    jubildb
)

target_compile_options(jubectl
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Tests ------------------------------------------------------------------------
if(JUBILANT_BUILD_TESTS)
  enable_testing()

  set(JUBILANT_TEST_SOURCES
    tests/btree_tests.cpp
    tests/checkpoint_tests.cpp
    tests/lock_manager_tests.cpp
    tests/manifest_tests.cpp
    tests/pager_tests.cpp
    tests/simple_store_tests.cpp
    tests/transaction_context_tests.cpp
    tests/value_log_tests.cpp
    tests/wal_tests.cpp
  )

  add_executable(unit_tests
    ${JUBILANT_TEST_SOURCES}
  )

  target_link_libraries(unit_tests
    PRIVATE
      jubildb
      GTest::gtest_main
  )

  add_dependencies(unit_tests generate_schemas)

  include(GoogleTest)
  gtest_discover_tests(unit_tests)
endif()

file(GLOB_RECURSE JUBILANT_FORMAT_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
)

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  add_custom_target(clang-format
    COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${JUBILANT_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running clang-format on project sources"
  )

  add_custom_target(clang-format-check
    COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${JUBILANT_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking clang-format compliance"
  )
else()
  message(STATUS "clang-format not found; clang-format targets skipped")
endif()
