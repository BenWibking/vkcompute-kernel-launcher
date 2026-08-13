if(VTHOMAS_BUILD_RUNTIME)
  add_executable(vthomas_abi_test RuntimeAbiTest.cpp)
  target_link_libraries(vthomas_abi_test PRIVATE vthomas::runtime)
  add_test(NAME vthomas.abi COMMAND vthomas_abi_test)
endif()

if(VTHOMAS_BUILD_LLVM_PLUGIN)
  if(TARGET opt)
    set(VTHOMAS_OPT_EXECUTABLE "$<TARGET_FILE:opt>")
  else()
    find_program(VTHOMAS_OPT_EXECUTABLE opt HINTS "${LLVM_TOOLS_BINARY_DIR}")
  endif()

  if(VTHOMAS_OPT_EXECUTABLE)
    add_test(
      NAME vthomas.pass.discovery
      COMMAND "${CMAKE_COMMAND}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/discovery.ll"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPassTest.cmake")
  endif()
endif()
