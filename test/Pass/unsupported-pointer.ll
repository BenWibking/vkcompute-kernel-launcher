%vthomas.signature_argument_tag.index = type { [29 x i8], i64 }
%vthomas.signature_argument_tag.rw_f64 = type { [38 x i8], double }
%vthomas.signature_arguments = type {
  %vthomas.signature_argument_tag.index,
  %vthomas.signature_argument_tag.rw_f64
}
%vthomas.signature_record = type { ptr, %vthomas.signature_arguments }

@__vthomas_signature_bad_pointer = constant %vthomas.signature_record {
  ptr @bad_pointer, %vthomas.signature_arguments zeroinitializer
}

define void @bad_pointer(i64 %i, ptr %generic) #0 {
  ret void
}

attributes #0 = { "vthomas.kernel" }
