file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(lowered "${OUTPUT_DIR}/lowered.ll")
set(raw_spirv "${OUTPUT_DIR}/raw.spv")
set(final_spirv "${OUTPUT_DIR}/final.spv")
set(second_spirv "${OUTPUT_DIR}/final-twice.spv")

execute_process(
  COMMAND "${OPT}" -load-pass-plugin=${PLUGIN} -passes=vthomas-lower -S
          "${INPUT}" -o "${lowered}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "vthomas-lower failed:\n${error}")
endif()

execute_process(
  COMMAND "${LLC}" -O3 -mtriple=spirv1.6-unknown-vulkan1.3-compute
          -filetype=obj "${lowered}" -o "${raw_spirv}"
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
  COMMAND "${FINALIZER}" "${final_spirv}" "${second_spirv}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "second VTHOMAS SPIR-V finalization failed:\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${final_spirv}" "${second_spirv}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "VTHOMAS SPIR-V finalization is not idempotent")
endif()

execute_process(
  COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${final_spirv}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spirv-val rejected the VTHOMAS module:\n${error}")
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
    "OpEntryPoint GLCompute.*\"vthomas_saxpy\""
    "OpExecutionMode.*LocalSize 256 1 1"
    "BuiltIn GlobalInvocationId"
    "DescriptorSet 0"
    "Binding 0"
    "Binding 1"
    "StorageBuffer"
    "PushConstant")
  if(NOT output MATCHES "${pattern}")
    message(FATAL_ERROR
      "validated SPIR-V is missing '${pattern}':\n${output}")
  endif()
endforeach()

string(REGEX MATCHALL "OpDecorate [^\n]+ Aliased" alias_decorations
       "${output}")
list(LENGTH alias_decorations alias_count)
if(NOT alias_count EQUAL 2)
  message(FATAL_ERROR
    "expected one Aliased decoration per storage buffer:\n${output}")
endif()

string(REGEX MATCHALL "Op(Member)?Decorate [^\n]+ NonWritable" readonly_decorations
       "${output}")
list(LENGTH readonly_decorations readonly_count)
if(NOT readonly_count EQUAL 1)
  message(FATAL_ERROR
    "expected exactly one read-only storage buffer:\n${output}")
endif()

if(output MATCHES "OpDecorate [^\n]+ Restrict")
  message(FATAL_ERROR
    "VTHOMAS unexpectedly marked a storage buffer Restrict:\n${output}")
endif()
