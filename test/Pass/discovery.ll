; RUN: opt -load-pass-plugin=%vthomas_plugin -passes=vthomas-lower -S %s | FileCheck %s

@.kernel = private unnamed_addr constant [15 x i8] c"vthomas.kernel\00"
@.file = private unnamed_addr constant [9 x i8] c"test.cpp\00"
@llvm.global.annotations = appending global [1 x { ptr, ptr, ptr, i32, ptr }] [
  { ptr, ptr, ptr, i32, ptr }
  { ptr @saxpy, ptr @.kernel, ptr @.file, i32 1, ptr null }
], section "llvm.metadata"

define void @saxpy(i64 %i, ptr addrspace(11) %y) {
  ret void
}

; CHECK: define void @saxpy(i64 %i, ptr addrspace(11) %y) #[[ATTR:[0-9]+]]
; CHECK: attributes #[[ATTR]] = { "vthomas.kernel" }

