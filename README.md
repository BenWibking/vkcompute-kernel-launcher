# VTHOMAS

VTHOMAS is an experimental ordinary-C++ to Vulkan-compute ABI bridge. It uses
Clang as the only source frontend, lowers marked kernel boundaries in an LLVM
24 pass, and emits Vulkan 1.3 SPIR-V through LLVM's upstream SPIR-V backend.

Implemented compiler and ABI features include:

- automatic, debug-info-independent signature metadata for opaque pointers;
- one-dimensional kernels with a fixed `256 x 1 x 1` workgroup;
- bounds guarding with a packed 64-bit invocation count;
- one set-0 storage-buffer descriptor per `gptr<T>` argument;
- const/read-only and mutable/read-write buffer reflection;
- arithmetic and standard-layout, trivially-copyable POD buffer elements;
- 8-, 16-, 32-, and 64-bit integer, enum, `float`, and `double` scalars;
- 64-bit interior-buffer byte offsets in push constants;
- compiler-owned `!vthomas.reflection` metadata and matching host descriptors;
- conservative SPIR-V `Aliased` decoration for every storage buffer;
- an injectable runtime backend with deterministic argument packing.

The repository does not yet contain a platform implementation of
`runtime_backend` for HIP DMA-BUF import, Vulkan pipeline creation, and RADV
submission. Those operations require a Linux AMD/HIP/Vulkan host. The compiler
pipeline and backend-neutral host ABI are validated here; RADV execution and
ACO ISA confirmation remain hardware integration work.

## Source API

Compile the same source once for the host and once with `VTHOMAS_DEVICE` for
the device:

```cpp
#include "vthomas/vthomas.hpp"

VTHOMAS_KERNEL void saxpy(vthomas::index_type i,
                          vthomas::gptr<double> y,
                          vthomas::gptr<const double> x,
                          double a) {
  y[i] += a * x[i];
}

VTHOMAS_REGISTER_KERNEL(saxpy);
```

The host launch is:

```cpp
vthomas::parallel_for(n, saxpy, y, x, a);
```

`VTHOMAS_REGISTER_KERNEL` is required in both compilation passes. At present,
its argument must be an unqualified, non-overloaded function identifier. The
host pass registers the descriptor used by `parallel_for`; the device pass
emits removable type tags that preserve pointee types through LLVM opaque
pointers.

Install a `vthomas::runtime_backend` before launching. The backend receives the
kernel descriptor, pointer arguments with their minimum byte ranges, and the
partially packed push constants. After resolving allocation bases and interior
offsets, it can use `vthomas::apply_buffer_offsets` to produce the final payload.

## Build against LLVM 24

The tested configuration uses the LLVM checkout at
`/Users/benwibking/github/llvm-project` as an external project. The SPIR-V
backend is experimental and must be enabled explicitly:

```sh
cmake -S /Users/benwibking/github/llvm-project/llvm -B build/llvm \
  -G Ninja \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=Native \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=SPIRV \
  -DLLVM_EXTERNAL_PROJECTS=vthomas \
  -DLLVM_EXTERNAL_VTHOMAS_SOURCE_DIR="$PWD"

cmake --build build/llvm --target \
  VThomasLower vthomas_runtime vthomas_spirv_finalize opt llc \
  vthomas_abi_test
ctest --test-dir build/llvm/tools/vthomas/test --output-on-failure
```

A standalone configuration can instead set `LLVM_DIR` to an LLVM 24 build or
install tree. The runtime-only library has no LLVM, HIP, or Vulkan dependency:

```sh
cmake -S . -B build/runtime \
  -DVTHOMAS_BUILD_LLVM_PLUGIN=OFF \
  -DVTHOMAS_BUILD_TOOLS=OFF
cmake --build build/runtime
ctest --test-dir build/runtime --output-on-failure
```

The SPIR-V validation tests are registered when `llc`, `spirv-val`, and
`spirv-dis` are available.

## Device compilation pipeline

The validated development pipeline is:

```sh
clang++ -std=c++20 -DVTHOMAS_DEVICE -fno-exceptions -fno-rtti \
  -O1 -S -emit-llvm -Iinclude examples/saxpy.cpp -o saxpy.ll

opt -load-pass-plugin=/path/to/VThomasLower.dylib \
  -passes=vthomas-lower -S saxpy.ll -o saxpy.lowered.ll

llc -O3 -mtriple=spirv1.6-unknown-vulkan1.3-compute \
  -filetype=obj saxpy.lowered.ll -o saxpy.raw.spv

vthomas_spirv_finalize saxpy.raw.spv saxpy.spv
spirv-val --target-env vulkan1.3 saxpy.spv
```

The finalizer is required for the MVP aliasing contract. LLVM 24 creates the
descriptor `OpVariable` internally, so the LLVM IR pass cannot attach `Aliased`
to that declaration directly. The small finalizer decorates every generated
set/binding storage resource and is idempotence-tested. This preserves legal
overlap between separate C++ pointer arguments under the SPIR-V
[aliasing rules](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#AliasingSection).

## Supported boundary

The pass deliberately rejects generic pointers, pointer-to-pointer arguments,
varargs, unsupported scalar widths, nontrivial POD elements, and non-inlined
helper calls that carry `gptr<T>`. Top-level POD field access is supported;
pointer-rich objects, pointer chasing, captured lambdas, device exceptions,
RTTI, recursion, and physical storage-buffer pointers are not.

See [`vthomas_llvm_vulkan_design.md`](vthomas_llvm_vulkan_design.md) for the
architecture, runtime interop plan, and hardware definition of success.
