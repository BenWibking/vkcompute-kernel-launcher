cmake_minimum_required(VERSION 3.20)

project(VThomas VERSION 0.1.0 LANGUAGES CXX)

option(VTHOMAS_BUILD_LLVM_PLUGIN "Build the LLVM ABI-lowering pass plugin" ON)
option(VTHOMAS_BUILD_RUNTIME "Build the host runtime library" ON)
option(VTHOMAS_BUILD_TOOLS "Build VTHOMAS SPIR-V post-processing tools" ON)
option(VTHOMAS_BUILD_TESTS "Build VTHOMAS tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vthomas_headers INTERFACE)
add_library(vthomas::headers ALIAS vthomas_headers)
target_include_directories(vthomas_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

if(VTHOMAS_BUILD_RUNTIME)
  add_subdirectory(lib/Runtime)
endif()

if(VTHOMAS_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(VTHOMAS_BUILD_LLVM_PLUGIN)
  # When VTHOMAS is configured through LLVM_EXTERNAL_PROJECTS, LLVM has already
  # supplied these helpers. A standalone build instead consumes LLVMConfig.cmake
  # from the matching LLVM build or install tree.
  if(NOT COMMAND add_llvm_pass_plugin)
    find_package(LLVM 24 REQUIRED CONFIG)
    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
    include(AddLLVM)
  endif()

  if(NOT LLVM_VERSION_MAJOR EQUAL 24)
    message(FATAL_ERROR
      "VTHOMAS currently targets LLVM 24 from ../llvm-project; found LLVM ${LLVM_PACKAGE_VERSION}")
  endif()

  add_subdirectory(lib/Pass)
endif()

if(VTHOMAS_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(test)
  endif()
endif()
