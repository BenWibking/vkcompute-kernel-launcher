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
      NAME vthomas.pass.lowering
      COMMAND "${CMAKE_COMMAND}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/lowering.ll"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPassTest.cmake")

    add_test(
      NAME vthomas.pass.missing_signature
      COMMAND "${CMAKE_COMMAND}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/missing-signature.ll"
        "-DEXPECT=missing VTHOMAS_REGISTER_KERNEL"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPassFailureTest.cmake")

    add_test(
      NAME vthomas.pass.unsupported_pointer
      COMMAND "${CMAKE_COMMAND}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/unsupported-pointer.ll"
        "-DEXPECT=must be gptr<T>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPassFailureTest.cmake")

    add_test(
      NAME vthomas.pass.unsupported_helper_pointer
      COMMAND "${CMAKE_COMMAND}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/unsupported-helper-call.ll"
        "-DEXPECT=non-inlined helper call carrying a gptr<T>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPassFailureTest.cmake")

    add_test(
      NAME vthomas.pass.pointer_pod
      COMMAND "${CMAKE_COMMAND}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/pointer-pod.ll"
        "-DEXPECT=pointer-containing POD buffer element"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPassFailureTest.cmake")

    add_test(
      NAME vthomas.source.signature_metadata
      COMMAND "${CMAKE_COMMAND}"
        "-DCLANGXX=${CMAKE_CXX_COMPILER}"
        "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
        "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
        "-DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/examples/saxpy.cpp"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/source-integration"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunSourceTest.cmake")
  endif()

  if(TARGET llc AND TARGET vthomas_spirv_finalize)
    find_program(VTHOMAS_SPIRV_VAL spirv-val)
    find_program(VTHOMAS_SPIRV_DIS spirv-dis)
    if(VTHOMAS_SPIRV_VAL AND VTHOMAS_SPIRV_DIS)
      add_test(
        NAME vthomas.spirv.validation
        COMMAND "${CMAKE_COMMAND}"
          "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
          "-DLLC=$<TARGET_FILE:llc>"
          "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
          "-DFINALIZER=$<TARGET_FILE:vthomas_spirv_finalize>"
          "-DSPIRV_VAL=${VTHOMAS_SPIRV_VAL}"
          "-DSPIRV_DIS=${VTHOMAS_SPIRV_DIS}"
          "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Pass/lowering.ll"
          "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/spirv-validation"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/RunSpirvTest.cmake")
      set_tests_properties(vthomas.spirv.validation PROPERTIES
        DEPENDS vthomas.pass.lowering)

      foreach(source_test IN ITEMS saxpy pod scalars)
        if(source_test STREQUAL "saxpy")
          set(source_file "${PROJECT_SOURCE_DIR}/examples/saxpy.cpp")
          set(entry_point "vthomas_saxpy")
          set(buffer_count 2)
          set(expected_type "f64")
        elseif(source_test STREQUAL "pod")
          set(source_file "${PROJECT_SOURCE_DIR}/examples/pod.cpp")
          set(entry_point "vthomas_update_particles")
          set(buffer_count 1)
          set(expected_type "pod")
        else()
          set(source_file "${PROJECT_SOURCE_DIR}/examples/scalars.cpp")
          set(entry_point "vthomas_scalar_mix")
          set(buffer_count 1)
          set(expected_type "i8")
        endif()
        add_test(
          NAME vthomas.source.${source_test}.spirv_validation
          COMMAND "${CMAKE_COMMAND}"
            "-DCLANGXX=${CMAKE_CXX_COMPILER}"
            "-DOPT=${VTHOMAS_OPT_EXECUTABLE}"
            "-DLLC=$<TARGET_FILE:llc>"
            "-DPLUGIN=$<TARGET_FILE:VThomasLower>"
            "-DFINALIZER=$<TARGET_FILE:vthomas_spirv_finalize>"
            "-DSPIRV_VAL=${VTHOMAS_SPIRV_VAL}"
            "-DSPIRV_DIS=${VTHOMAS_SPIRV_DIS}"
            "-DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include"
            "-DSOURCE=${source_file}"
            "-DENTRY_POINT=${entry_point}"
            "-DBUFFER_COUNT=${buffer_count}"
            "-DEXPECTED_TYPE=${expected_type}"
            "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/source-${source_test}-spirv"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunSourceSpirvTest.cmake")
      endforeach()
    endif()
  endif()
endif()
