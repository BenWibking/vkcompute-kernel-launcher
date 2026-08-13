execute_process(
  COMMAND "${OPT}" -load-pass-plugin=${PLUGIN} -passes=vthomas-lower -S
          "${INPUT}" -o -
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "vthomas-lower failed:\n${error}")
endif()

if(NOT output MATCHES "attributes #[0-9]+ = \\{ \"vthomas.kernel\" \\}")
  message(FATAL_ERROR
    "vthomas-lower did not normalize the kernel annotation:\n${output}")
endif()
