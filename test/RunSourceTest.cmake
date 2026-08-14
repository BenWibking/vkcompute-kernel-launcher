file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(device_ir "${OUTPUT_DIR}/device.ll")

execute_process(
  COMMAND "${CLANGXX}" -std=c++20 -DVTHOMAS_DEVICE -fno-exceptions
          -fno-rtti -O1 -S -emit-llvm "-I${INCLUDE_DIR}" "${SOURCE}"
          -o "${device_ir}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ordinary C++ device compilation failed:\n${error}")
endif()

execute_process(
  COMMAND "${OPT}" -load-pass-plugin=${PLUGIN} -passes=vthomas-lower -S
          "${device_ir}" -o -
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "source-generated signature lowering failed:\n${error}")
endif()

if(NOT output MATCHES "define dso_local void @vthomas_saxpy\\(\\)" OR
   NOT output MATCHES "target\\(\"spirv.VulkanBuffer\", \\[0 x double\\], 12, 1\\)" OR
   NOT output MATCHES "target\\(\"spirv.VulkanBuffer\", \\[0 x double\\], 12, 0\\)" OR
   NOT output MATCHES "!vthomas.reflection")
  message(FATAL_ERROR
    "source-generated metadata did not lower to the expected ABI:\n${output}")
endif()

if(output MATCHES "@__vthomas_signature_saxpy" OR
   output MATCHES "@llvm.global.annotations")
  message(FATAL_ERROR
    "source signature metadata was not removed after lowering:\n${output}")
endif()
