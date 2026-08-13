add_llvm_pass_plugin(VThomasLower
  VThomasLower.cpp
  DEPENDS
  analysis_gen
  intrinsics_gen
  vt_gen
  BUILDTREE_ONLY)

target_compile_features(VThomasLower PRIVATE cxx_std_20)
