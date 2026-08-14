execute_process(
  COMMAND "${OPT}" -load-pass-plugin=${PLUGIN} -passes=vthomas-lower -S
          "${INPUT}" -o -
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "vthomas-lower failed:\n${error}")
endif()

if(NOT output MATCHES "define dso_local void @vthomas_saxpy\\(\\)")
  message(FATAL_ERROR
    "vthomas-lower did not synthesize the Vulkan entry point:\n${output}")
endif()

if(output MATCHES "define void @saxpy\\(" OR
   output MATCHES "call[^\n]*@saxpy\\(")
  message(FATAL_ERROR
    "vthomas-lower did not inline and remove the source kernel:\n${output}")
endif()

if(NOT output MATCHES "@__vthomas_push_constants = external addrspace\\(13\\) externally_initialized global %vthomas.push_constants")
  message(FATAL_ERROR
    "vthomas-lower did not emit the push-constant block:\n${output}")
endif()

if(NOT output MATCHES "llvm.spv.thread.id.i32\\(i32 0\\)" OR
   NOT output MATCHES "llvm.spv.resource.handlefrombinding" OR
   NOT output MATCHES "llvm.spv.resource.getpointer")
  message(FATAL_ERROR
    "vthomas-lower did not emit the Vulkan resource ABI:\n${output}")
endif()

if(NOT output MATCHES "icmp ult i64" OR
   NOT output MATCHES "udiv i64" OR
   NOT output MATCHES "\"hlsl.numthreads\"=\"256,1,1\"" OR
   NOT output MATCHES "\"hlsl.shader\"=\"compute\"")
  message(FATAL_ERROR
    "vthomas-lower did not emit bounds, offsets, and compute attributes:\n${output}")
endif()

if(NOT output MATCHES "!vthomas.reflection = !\\{![0-9]+\\}" OR
   NOT output MATCHES "!\"read_write\"" OR
   NOT output MATCHES "!\"read_only\"")
  message(FATAL_ERROR
    "vthomas-lower did not emit compiler-owned reflection:\n${output}")
endif()

if(output MATCHES "@__vthomas_signature_saxpy" OR
   output MATCHES "@llvm.global.annotations")
  message(FATAL_ERROR
    "vthomas-lower left removable source metadata in the module:\n${output}")
endif()
