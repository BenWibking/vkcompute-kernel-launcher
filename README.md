# VTHOMAS

VTHOMAS is an experimental ordinary-C++ to Vulkan-compute ABI bridge. This
repository currently contains the project skeleton described in
[`vthomas_llvm_vulkan_design.md`](vthomas_llvm_vulkan_design.md):

- an LLVM 24 new-pass-manager plugin named `VThomasLower`;
- source-facing kernel and address-space declarations;
- shared compiler/runtime ABI descriptions and 32-bit scalar packing;
- an injectable host-runtime backend boundary.

The pass currently performs the first, deliberately small transformation: it
discovers `annotate("vthomas.kernel")` entries and attaches the normalized
`"vthomas.kernel"` LLVM function attribute. Vulkan entry-point synthesis,
signature type tags, reflection emission, and HIP/Vulkan interop are the next
implementation stages and are not represented as working features.

## Build against the LLVM checkout

The plugin targets the LLVM 24 development checkout at
`/Users/benwibking/github/llvm-project`. The most reliable build is as an LLVM
external project:

```sh
cmake -S /Users/benwibking/github/llvm-project/llvm -B build/llvm \
  -G Ninja \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=Native \
  -DLLVM_EXTERNAL_PROJECTS=vthomas \
  -DLLVM_EXTERNAL_VTHOMAS_SOURCE_DIR="$PWD"
cmake --build build/llvm --target VThomasLower vthomas_runtime
cmake --build build/llvm --target opt vthomas_abi_test
ctest --test-dir build/llvm/tools/vthomas --output-on-failure
```

If that LLVM checkout has already been built, a standalone configuration also
works by pointing `LLVM_DIR` at its generated CMake package:

```sh
cmake -S . -B build -G Ninja \
  -DLLVM_DIR=/path/to/llvm-build/lib/cmake/llvm
cmake --build build
ctest --test-dir build --output-on-failure
```

The runtime-only part has no LLVM, HIP, or Vulkan dependency:

```sh
cmake -S . -B build/runtime -DVTHOMAS_BUILD_LLVM_PLUGIN=OFF
cmake --build build/runtime
ctest --test-dir build/runtime --output-on-failure
```

Once built, the discovery pass can be invoked with:

```sh
opt -load-pass-plugin=/path/to/VThomasLower.dylib \
  -passes=vthomas-lower -S input.ll -o output.ll
```
