%bad.element = type { ptr }
%vthomas.signature_argument_tag.index = type { [29 x i8], i64 }
%vthomas.signature_argument_tag.bad = type { [42 x i8], %bad.element }
%vthomas.signature_arguments = type {
  %vthomas.signature_argument_tag.index,
  %vthomas.signature_argument_tag.bad
}
%vthomas.signature_record = type { ptr, %vthomas.signature_arguments }

@.kernel = private unnamed_addr constant [15 x i8] c"vthomas.kernel\00"
@.file = private unnamed_addr constant [9 x i8] c"test.cpp\00"
@llvm.global.annotations = appending global [1 x { ptr, ptr, ptr, i32, ptr }] [
  { ptr, ptr, ptr, i32, ptr }
  { ptr @pointer_pod, ptr @.kernel, ptr @.file, i32 1, ptr null }
], section "llvm.metadata"
@__vthomas_signature_pointer_pod = constant %vthomas.signature_record {
  ptr @pointer_pod, %vthomas.signature_arguments zeroinitializer
}
@llvm.compiler.used = appending global [1 x ptr] [
  ptr @__vthomas_signature_pointer_pod
], section "llvm.metadata"

define void @pointer_pod(i64 %i, ptr addrspace(11) %elements) {
entry:
  ret void
}
