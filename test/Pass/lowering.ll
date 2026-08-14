; The marker byte counts encode u64, read-write f64, read-only f64, and f64.
%vthomas.signature_argument_tag.index = type { [29 x i8], i64 }
%vthomas.signature_argument_tag.rw_f64 = type { [38 x i8], double }
%vthomas.signature_argument_tag.ro_f64 = type { [39 x i8], double }
%vthomas.signature_argument_tag.f64 = type { [37 x i8], double }
%vthomas.signature_arguments = type {
  %vthomas.signature_argument_tag.index,
  %vthomas.signature_argument_tag.rw_f64,
  %vthomas.signature_argument_tag.ro_f64,
  %vthomas.signature_argument_tag.f64
}
%vthomas.signature_record = type { ptr, %vthomas.signature_arguments }

@.kernel = private unnamed_addr constant [15 x i8] c"vthomas.kernel\00"
@.file = private unnamed_addr constant [9 x i8] c"test.cpp\00"
@llvm.global.annotations = appending global [1 x { ptr, ptr, ptr, i32, ptr }] [
  { ptr, ptr, ptr, i32, ptr }
  { ptr @saxpy, ptr @.kernel, ptr @.file, i32 1, ptr null }
], section "llvm.metadata"

@__vthomas_signature_saxpy = constant %vthomas.signature_record {
  ptr @saxpy, %vthomas.signature_arguments zeroinitializer
}
@llvm.compiler.used = appending global [1 x ptr] [
  ptr @__vthomas_signature_saxpy
], section "llvm.metadata"

define void @saxpy(i64 %i, ptr addrspace(11) %y,
                   ptr addrspace(11) %x, double %a) {
entry:
  %x.ptr = getelementptr double, ptr addrspace(11) %x, i64 %i
  %x.value = load double, ptr addrspace(11) %x.ptr, align 8
  %scaled = fmul double %x.value, %a
  %y.ptr = getelementptr double, ptr addrspace(11) %y, i64 %i
  %y.value = load double, ptr addrspace(11) %y.ptr, align 8
  %sum = fadd double %y.value, %scaled
  store double %sum, ptr addrspace(11) %y.ptr, align 8
  ret void
}
