%vthomas.signature_argument_tag.index = type { [29 x i8], i64 }
%vthomas.signature_argument_tag.rw_f64 = type { [38 x i8], double }
%vthomas.signature_arguments = type {
  %vthomas.signature_argument_tag.index,
  %vthomas.signature_argument_tag.rw_f64
}
%vthomas.signature_record = type { ptr, %vthomas.signature_arguments }

@.kernel = private unnamed_addr constant [15 x i8] c"vthomas.kernel\00"
@.file = private unnamed_addr constant [9 x i8] c"test.cpp\00"
@llvm.global.annotations = appending global [1 x { ptr, ptr, ptr, i32, ptr }] [
  { ptr, ptr, ptr, i32, ptr }
  { ptr @calls_helper, ptr @.kernel, ptr @.file, i32 1, ptr null }
], section "llvm.metadata"
@__vthomas_signature_calls_helper = constant %vthomas.signature_record {
  ptr @calls_helper, %vthomas.signature_arguments zeroinitializer
}
@llvm.compiler.used = appending global [1 x ptr] [
  ptr @__vthomas_signature_calls_helper
], section "llvm.metadata"

define void @helper(ptr addrspace(11) %output, i64 %i) noinline {
entry:
  %element = getelementptr double, ptr addrspace(11) %output, i64 %i
  store double 1.0, ptr addrspace(11) %element
  ret void
}

define void @calls_helper(i64 %i, ptr addrspace(11) %output) {
entry:
  call void @helper(ptr addrspace(11) %output, i64 %i)
  ret void
}
