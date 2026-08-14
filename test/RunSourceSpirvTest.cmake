file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(device_ir "${OUTPUT_DIR}/device.ll")
set(lowered_ir "${OUTPUT_DIR}/lowered.ll")
set(raw_spirv "${OUTPUT_DIR}/raw.spv")
set(final_spirv "${OUTPUT_DIR}/final.spv")

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
          "${device_ir}" -o "${lowered_ir}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "source-generated signature lowering failed:\n${error}")
endif()

file(READ "${lowered_ir}" lowered)
if(NOT lowered MATCHES "!\"${EXPECTED_TYPE}\"")
  message(FATAL_ERROR
    "source reflection is missing ABI type '${EXPECTED_TYPE}':\n${lowered}")
endif()

execute_process(
  COMMAND "${LLC}" -O3 -mtriple=spirv1.6-unknown-vulkan1.3-compute
          -filetype=obj "${lowered_ir}" -o "${raw_spirv}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "LLVM SPIR-V lowering failed:\n${error}")
endif()

execute_process(
  COMMAND "${FINALIZER}" "${raw_spirv}" "${final_spirv}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "VTHOMAS SPIR-V finalization failed:\n${error}")
endif()

execute_process(
  COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${final_spirv}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spirv-val rejected source-generated SPIR-V:\n${error}")
endif()

execute_process(
  COMMAND "${SPIRV_DIS}" "${final_spirv}" -o -
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spirv-dis failed:\n${error}")
endif()

foreach(pattern
    "OpEntryPoint GLCompute.*\"${ENTRY_POINT}\""
    "OpExecutionMode.*LocalSize 256 1 1"
    "BuiltIn GlobalInvocationId"
    "PushConstant")
  if(NOT output MATCHES "${pattern}")
    message(FATAL_ERROR
      "source-generated SPIR-V is missing '${pattern}':\n${output}")
  endif()
endforeach()

string(REGEX MATCHALL "OpDecorate [^\n]+ Aliased" alias_decorations
       "${output}")
list(LENGTH alias_decorations alias_count)
if(NOT alias_count EQUAL BUFFER_COUNT)
  message(FATAL_ERROR
    "expected ${BUFFER_COUNT} Aliased storage buffers:\n${output}")
endif()
