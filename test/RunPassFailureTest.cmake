execute_process(
  COMMAND "${OPT}" -load-pass-plugin=${PLUGIN} -passes=vthomas-lower
          -disable-output "${INPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR
    "vthomas-lower unexpectedly accepted unsupported input:\n${output}")
endif()

if(NOT error MATCHES "${EXPECT}")
  message(FATAL_ERROR
    "vthomas-lower failed without the expected '${EXPECT}' diagnostic:\n${error}")
endif()
