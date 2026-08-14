#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

using namespace llvm;

namespace {

constexpr StringLiteral KernelAnnotation = "vthomas.kernel";
constexpr StringLiteral SignaturePrefix = "__vthomas_signature_";
constexpr StringLiteral ReflectionMetadata = "vthomas.reflection";
constexpr unsigned StorageBufferAddressSpace = 11;
constexpr unsigned PushConstantAddressSpace = 13;
constexpr unsigned StorageBufferClass = 12;
constexpr unsigned WorkgroupSize = 256;

enum class ArgumentRole : uint8_t {
  Scalar = 1,
  ReadWriteBuffer = 2,
  ReadOnlyBuffer = 3
};

struct SignatureArgument {
  ArgumentRole Role;
  unsigned TypeCode;
  Type *ValueType;
};

struct KernelInfo {
  Function *Kernel = nullptr;
  std::string SourceName;
  std::string EntryName;
  SmallVector<SignatureArgument> Arguments;
  SmallVector<unsigned> WordOffsets;
  unsigned PushConstantWords = 0;
};

struct BufferPointerLowering {
  Value *Pointer;
  Value *Handle;
  Value *ElementOffset;
  Type *ElementType;
};

bool isTypeCodeCompatible(unsigned TypeCode, Type *T, bool IsBuffer);

[[noreturn]] void fail(const Function &Kernel, const Twine &Message) {
  report_fatal_error(Twine("VTHOMAS kernel '") + Kernel.getName() +
                     "': " + Message);
}

StringRef getConstantCString(const Value *V) {
  V = V->stripPointerCasts();
  const auto *GV = dyn_cast<GlobalVariable>(V);
  if (GV == nullptr || !GV->hasInitializer())
    return {};

  const auto *Data = dyn_cast<ConstantDataSequential>(GV->getInitializer());
  if (Data == nullptr || !Data->isCString())
    return {};
  return Data->getAsCString();
}

Function *findFunction(Constant *C) {
  if (auto *F = dyn_cast<Function>(C->stripPointerCasts()))
    return F;
  for (Value *Operand : C->operands())
    if (auto *Child = dyn_cast<Constant>(Operand))
      if (Function *F = findFunction(Child))
        return F;
  return nullptr;
}

bool decodeSignatureTag(Type *MarkerType, Type *ValueType,
                        SignatureArgument &Argument) {
  auto *Marker = dyn_cast<ArrayType>(MarkerType);
  if (Marker == nullptr || !Marker->getElementType()->isIntegerTy(8))
    return false;

  const uint64_t Encoded = Marker->getNumElements();
  if (Encoded == 0)
    return false;
  const unsigned RoleValue = 1 + ((Encoded - 1) % 4);
  if (RoleValue > static_cast<unsigned>(ArgumentRole::ReadOnlyBuffer))
    return false;

  Argument.Role = static_cast<ArgumentRole>(RoleValue);
  Argument.TypeCode = static_cast<unsigned>((Encoded - RoleValue) / 4);
  Argument.ValueType = ValueType;
  return Argument.TypeCode <= 10 &&
         isTypeCodeCompatible(Argument.TypeCode, ValueType,
                              Argument.Role != ArgumentRole::Scalar);
}

void collectSignatureTags(Type *T, SmallVectorImpl<SignatureArgument> &Result) {
  auto *ST = dyn_cast<StructType>(T);
  if (ST == nullptr || ST->isOpaque())
    return;

  for (unsigned I = 0; I < ST->getNumElements(); ++I) {
    if (I + 1 < ST->getNumElements()) {
      SignatureArgument Argument{};
      if (decodeSignatureTag(ST->getElementType(I), ST->getElementType(I + 1),
                             Argument)) {
        Result.push_back(Argument);
        ++I;
        continue;
      }
    }
    collectSignatureTags(ST->getElementType(I), Result);
  }
}

std::string makeEntryName(StringRef SourceName) {
  std::string Result = "vthomas_";
  static constexpr char Hex[] = "0123456789abcdef";
  for (unsigned char C : SourceName.bytes()) {
    if (std::isalnum(C) || C == '_') {
      Result.push_back(static_cast<char>(C));
      continue;
    }
    Result.push_back('_');
    Result.push_back(Hex[C >> 4]);
    Result.push_back(Hex[C & 0xf]);
  }
  return Result;
}

StringRef typeCodeName(unsigned TypeCode) {
  static constexpr StringLiteral Names[] = {"i8",  "u8",  "i16", "u16",
                                            "i32", "u32", "i64", "u64",
                                            "f32", "f64", "pod"};
  if (TypeCode >= std::size(Names))
    return "invalid";
  return Names[TypeCode];
}

bool isTypeCodeCompatible(unsigned TypeCode, Type *T, bool IsBuffer) {
  switch (TypeCode) {
  case 0:
  case 1:
    return T->isIntegerTy(8);
  case 2:
  case 3:
    return T->isIntegerTy(16);
  case 4:
  case 5:
    return T->isIntegerTy(32);
  case 6:
  case 7:
    return T->isIntegerTy(64);
  case 8:
    return T->isFloatTy();
  case 9:
    return T->isDoubleTy();
  case 10:
    return IsBuffer && T->isStructTy() && T->isSized();
  default:
    return false;
  }
}

bool containsPointer(Type *T, SmallPtrSetImpl<Type *> &Visited) {
  if (T->isPointerTy())
    return true;
  if (!Visited.insert(T).second)
    return false;
  if (auto *Array = dyn_cast<ArrayType>(T))
    return containsPointer(Array->getElementType(), Visited);
  if (auto *Structure = dyn_cast<StructType>(T)) {
    if (Structure->isOpaque())
      return false;
    return llvm::any_of(Structure->elements(), [&](Type *Element) {
      return containsPointer(Element, Visited);
    });
  }
  return false;
}

bool containsPointer(Type *T) {
  SmallPtrSet<Type *, 8> Visited;
  return containsPointer(T, Visited);
}

SmallPtrSet<Function *, 8> discoverKernels(Module &M) {
  SmallPtrSet<Function *, 8> Kernels;
  for (Function &F : M)
    if (F.hasFnAttribute(KernelAnnotation))
      Kernels.insert(&F);

  GlobalVariable *Annotations = M.getGlobalVariable("llvm.global.annotations");
  if (Annotations == nullptr || !Annotations->hasInitializer())
    return Kernels;
  const auto *Entries = dyn_cast<ConstantArray>(Annotations->getInitializer());
  if (Entries == nullptr)
    return Kernels;

  bool OnlyVThomasAnnotations = true;
  for (const Use &EntryUse : Entries->operands()) {
    const auto *Entry = dyn_cast<ConstantStruct>(EntryUse.get());
    if (Entry == nullptr || Entry->getNumOperands() < 2 ||
        getConstantCString(Entry->getOperand(1)) != KernelAnnotation) {
      OnlyVThomasAnnotations = false;
      continue;
    }
    auto *Kernel =
        dyn_cast<Function>(Entry->getOperand(0)->stripPointerCasts());
    if (Kernel == nullptr)
      continue;
    Kernel->addFnAttr(KernelAnnotation);
    Kernels.insert(Kernel);
  }

  // Removing the source marker lets the original device-only body become dead
  // after it has been inlined into the Vulkan entry point. Preserve the global
  // if it also contains annotations owned by another tool.
  if (OnlyVThomasAnnotations) {
    Annotations->dropAllReferences();
    Annotations->eraseFromParent();
  }
  return Kernels;
}

SmallVector<KernelInfo>
readKernelSignatures(Module &M, const SmallPtrSetImpl<Function *> &Discovered) {
  SmallVector<KernelInfo> Result;
  SmallVector<GlobalVariable *> MetadataGlobals;
  SmallPtrSet<Function *, 8> Registered;

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.getName().starts_with(SignaturePrefix) || !GV.hasInitializer())
      continue;
    Function *Kernel = findFunction(GV.getInitializer());
    if (Kernel == nullptr)
      report_fatal_error(Twine("VTHOMAS signature global '") + GV.getName() +
                         "' does not reference a kernel function");
    if (!Discovered.contains(Kernel))
      fail(*Kernel, "has signature metadata but is not marked VTHOMAS_KERNEL");
    if (!Registered.insert(Kernel).second)
      fail(*Kernel, "has more than one VTHOMAS signature registration");

    KernelInfo Info;
    Info.Kernel = Kernel;
    Info.SourceName = GV.getName().drop_front(SignaturePrefix.size()).str();
    Info.EntryName = makeEntryName(Info.SourceName);
    collectSignatureTags(GV.getValueType(), Info.Arguments);
    Result.push_back(std::move(Info));
    MetadataGlobals.push_back(&GV);
  }

  for (Function *Kernel : Discovered)
    if (!Registered.contains(Kernel))
      fail(*Kernel, "is missing VTHOMAS_REGISTER_KERNEL signature metadata");

  removeFromUsedLists(M, [&](Constant *C) {
    Value *V = C->stripPointerCasts();
    return llvm::is_contained(MetadataGlobals, V);
  });
  for (GlobalVariable *GV : MetadataGlobals) {
    GV->dropAllReferences();
    GV->eraseFromParent();
  }
  return Result;
}

void validateAndLayoutKernel(KernelInfo &Info, const DataLayout &DL) {
  Function &Kernel = *Info.Kernel;
  FunctionType *FT = Kernel.getFunctionType();
  if (!FT->getReturnType()->isVoidTy())
    fail(Kernel, "must return void");
  if (FT->isVarArg())
    fail(Kernel, "must not use varargs");
  if (FT->getNumParams() == 0)
    fail(Kernel, "must take the invocation index as its first parameter");
  if (Info.Arguments.size() != FT->getNumParams())
    fail(Kernel, formatv("signature metadata contains {0} arguments but the "
                         "function has {1}",
                         Info.Arguments.size(), FT->getNumParams()));

  Type *IndexType = FT->getParamType(0);
  const SignatureArgument &Index = Info.Arguments[0];
  if (Index.Role != ArgumentRole::Scalar ||
      !(IndexType->isIntegerTy(32) || IndexType->isIntegerTy(64)) ||
      Index.ValueType != IndexType ||
      !isTypeCodeCompatible(Index.TypeCode, Index.ValueType, false))
    fail(Kernel, "first parameter must be a registered 32-bit or 64-bit "
                 "integer invocation index");

  unsigned WordOffset = 2; // uint64_t invocation count
  Info.WordOffsets.resize(FT->getNumParams());
  Info.WordOffsets[0] = 0;
  for (unsigned I = 1; I < FT->getNumParams(); ++I) {
    Type *ParameterType = FT->getParamType(I);
    const SignatureArgument &Argument = Info.Arguments[I];
    Info.WordOffsets[I] = WordOffset;
    const bool IsBuffer = Argument.Role != ArgumentRole::Scalar;

    if (!isTypeCodeCompatible(Argument.TypeCode, Argument.ValueType, IsBuffer))
      fail(Kernel, formatv("argument {0} has an unsupported or inconsistent "
                           "type tag",
                           I));
    if (IsBuffer) {
      auto *Pointer = dyn_cast<PointerType>(ParameterType);
      if (Pointer == nullptr ||
          Pointer->getAddressSpace() != StorageBufferAddressSpace)
        fail(Kernel,
             formatv("argument {0} must be gptr<T> (address space 11)", I));
      if (!Argument.ValueType->isSized() ||
          DL.getTypeAllocSize(Argument.ValueType).isScalable())
        fail(Kernel, formatv("argument {0} has an unsized buffer element", I));
      if (Argument.TypeCode == 10 && containsPointer(Argument.ValueType))
        fail(Kernel, formatv("argument {0} has a pointer-containing POD "
                             "buffer element",
                             I));
      WordOffset += 2; // uint64_t interior byte offset
      continue;
    }

    if (ParameterType->isPointerTy() || ParameterType != Argument.ValueType)
      fail(Kernel, formatv("argument {0} scalar metadata does not match its "
                           "LLVM parameter type",
                           I));
    WordOffset +=
        divideCeil(ParameterType->getPrimitiveSizeInBits(), uint64_t{32});
  }

  if (WordOffset * sizeof(uint32_t) > 128)
    fail(Kernel, "push-constant ABI exceeds the Vulkan minimum limit of 128 "
                 "bytes");
  Info.PushConstantWords = WordOffset;
}

GlobalVariable *createPushConstantBlock(Module &M, unsigned WordCount,
                                        StructType *&BlockType) {
  LLVMContext &Context = M.getContext();
  Type *I32 = Type::getInt32Ty(Context);
  BlockType = StructType::create(Context, "vthomas.push_constants");
  BlockType->setBody({ArrayType::get(I32, WordCount)});
  auto *GV = new GlobalVariable(
      M, BlockType, false, GlobalValue::ExternalLinkage, nullptr,
      "__vthomas_push_constants", nullptr, GlobalValue::NotThreadLocal,
      PushConstantAddressSpace, true);
  GV->setExternallyInitialized(true);
  GV->setAlignment(Align(4));
  return GV;
}

Value *loadPushWord(IRBuilder<> &Builder, GlobalVariable *PushConstants,
                    StructType *BlockType, unsigned WordOffset) {
  Type *I32 = Builder.getInt32Ty();
  Value *Pointer = Builder.CreateInBoundsGEP(
      BlockType, PushConstants,
      {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(WordOffset)},
      "vthomas.pc.ptr");
  return Builder.CreateAlignedLoad(I32, Pointer, Align(4), "vthomas.pc.word");
}

Value *loadPushI64(IRBuilder<> &Builder, GlobalVariable *PushConstants,
                   StructType *BlockType, unsigned WordOffset) {
  Value *Low = Builder.CreateZExt(
      loadPushWord(Builder, PushConstants, BlockType, WordOffset),
      Builder.getInt64Ty(), "vthomas.pc.low");
  Value *High = Builder.CreateZExt(
      loadPushWord(Builder, PushConstants, BlockType, WordOffset + 1),
      Builder.getInt64Ty(), "vthomas.pc.high");
  High = Builder.CreateShl(High, 32, "vthomas.pc.high.shifted");
  return Builder.CreateOr(Low, High, "vthomas.pc.i64");
}

Value *loadScalar(IRBuilder<> &Builder, Type *Type,
                  GlobalVariable *PushConstants, StructType *BlockType,
                  unsigned WordOffset) {
  if (Type->isIntegerTy() && Type->getIntegerBitWidth() <= 32) {
    Value *Word = loadPushWord(Builder, PushConstants, BlockType, WordOffset);
    return Type->isIntegerTy(32)
               ? Word
               : Builder.CreateTrunc(Word, Type, "vthomas.scalar");
  }
  if (Type->isFloatTy())
    return Builder.CreateBitCast(
        loadPushWord(Builder, PushConstants, BlockType, WordOffset), Type,
        "vthomas.scalar");
  Value *Bits = loadPushI64(Builder, PushConstants, BlockType, WordOffset);
  if (Type->isIntegerTy(64))
    return Bits;
  if (Type->isDoubleTy())
    return Builder.CreateBitCast(Bits, Type, "vthomas.scalar");
  llvm_unreachable("validated VTHOMAS scalar type");
}

BufferPointerLowering createBufferPointer(IRBuilder<> &Builder, Module &M,
                                          const SignatureArgument &Argument,
                                          unsigned Binding, Value *ByteOffset,
                                          const DataLayout &DL,
                                          StringRef Name) {
  LLVMContext &Context = M.getContext();
  Type *RuntimeArray = ArrayType::get(Argument.ValueType, 0);
  const unsigned Writable = Argument.Role == ArgumentRole::ReadWriteBuffer;
  Type *ResourceType =
      TargetExtType::get(Context, "spirv.VulkanBuffer", {RuntimeArray},
                         {StorageBufferClass, Writable});
  Value *ResourceName = Builder.CreateGlobalString(Name, ".vthomas.resource");
  Value *Handle = Builder.CreateIntrinsic(
      ResourceType, Intrinsic::spv_resource_handlefrombinding,
      {Builder.getInt32(0), Builder.getInt32(Binding), Builder.getInt32(1),
       Builder.getInt32(0), ResourceName},
      nullptr, "vthomas.buffer.handle");
  Type *PointerType = PointerType::get(Context, StorageBufferAddressSpace);
  const uint64_t ElementBytes =
      DL.getTypeAllocSize(Argument.ValueType).getFixedValue();
  Value *ElementOffset = ByteOffset;
  if (ElementBytes != 1)
    ElementOffset =
        Builder.CreateUDiv(ByteOffset, Builder.getInt64(ElementBytes),
                           "vthomas.buffer.element_offset");
  Value *ElementOffsetI32 = Builder.CreateTrunc(
      ElementOffset, Builder.getInt32Ty(), "vthomas.buffer.element_offset.i32");
  Value *Pointer = Builder.CreateIntrinsic(
      PointerType, Intrinsic::spv_resource_getpointer,
      {Handle, ElementOffsetI32}, nullptr, "vthomas.buffer.pointer");
  return {Pointer, Handle, ElementOffset, Argument.ValueType};
}

void lowerTopLevelBufferGEPs(Function &Kernel,
                             ArrayRef<BufferPointerLowering> Buffers) {
  for (const BufferPointerLowering &Buffer : Buffers) {
    SmallVector<GetElementPtrInst *> Worklist;
    for (User *User : Buffer.Pointer->users())
      if (auto *GEP = dyn_cast<GetElementPtrInst>(User))
        Worklist.push_back(GEP);

    for (GetElementPtrInst *GEP : Worklist) {
      if (GEP->getSourceElementType() != Buffer.ElementType ||
          GEP->getNumIndices() == 0)
        fail(Kernel, "uses a buffer pointer with an unsupported top-level "
                     "pointer-arithmetic form");
      Value *Index = *GEP->idx_begin();
      if (!Index->getType()->isIntegerTy())
        fail(Kernel, "uses a non-integer storage-buffer index");

      IRBuilder<> Builder(GEP);
      Value *Index64 = Index;
      const unsigned IndexBits = Index->getType()->getIntegerBitWidth();
      if (IndexBits < 64)
        Index64 = Builder.CreateZExt(Index, Builder.getInt64Ty());
      else if (IndexBits > 64)
        Index64 = Builder.CreateTrunc(Index, Builder.getInt64Ty());
      Value *Combined = Builder.CreateAdd(Buffer.ElementOffset, Index64,
                                          "vthomas.buffer.combined_index");
      Value *CombinedI32 = Builder.CreateTrunc(Combined, Builder.getInt32Ty(),
                                               "vthomas.buffer.index.i32");
      Value *Pointer = Builder.CreateIntrinsic(
          PointerType::get(Kernel.getContext(), StorageBufferAddressSpace),
          Intrinsic::spv_resource_getpointer, {Buffer.Handle, CombinedI32},
          nullptr, "vthomas.buffer.element");

      // The resource intrinsic performs the array-element portion of the
      // access chain. Preserve any remaining aggregate indices (for example,
      // the field index in p[i].field) as an ordinary GEP rooted at element i.
      if (GEP->getNumIndices() > 1) {
        SmallVector<Value *> TailIndices;
        TailIndices.push_back(Builder.getInt32(0));
        auto Tail = std::next(GEP->idx_begin());
        TailIndices.append(Tail, GEP->idx_end());
        Pointer =
            GEP->isInBounds()
                ? Builder.CreateInBoundsGEP(Buffer.ElementType, Pointer,
                                            TailIndices, "vthomas.buffer.field")
                : Builder.CreateGEP(Buffer.ElementType, Pointer, TailIndices,
                                    "vthomas.buffer.field");
      }
      GEP->replaceAllUsesWith(Pointer);
      GEP->eraseFromParent();
    }
  }
}

void diagnoseUnsupportedPointerTransport(Function &Kernel, Function &Entry) {
  for (Instruction &Instruction : instructions(Entry)) {
    if (auto *Call = dyn_cast<CallBase>(&Instruction)) {
      Function *Callee = Call->getCalledFunction();
      if (Callee != nullptr && Callee->isIntrinsic())
        continue;
      for (Value *Argument : Call->args()) {
        auto *Pointer = dyn_cast<PointerType>(Argument->getType());
        if (Pointer != nullptr &&
            Pointer->getAddressSpace() == StorageBufferAddressSpace)
          fail(Kernel, "contains a non-inlined helper call carrying a gptr<T>");
      }
    }

    auto *Store = dyn_cast<StoreInst>(&Instruction);
    if (Store == nullptr)
      continue;
    auto *Pointer = dyn_cast<PointerType>(Store->getValueOperand()->getType());
    if (Pointer != nullptr &&
        Pointer->getAddressSpace() == StorageBufferAddressSpace)
      fail(Kernel, "stores a gptr<T> value; pointer-containing data is not "
                   "supported");
  }
}

Function *createEntryPoint(KernelInfo &Info, Module &M,
                           GlobalVariable *PushConstants, StructType *BlockType,
                           const DataLayout &DL) {
  Function &Kernel = *Info.Kernel;
  LLVMContext &Context = M.getContext();
  if (M.getFunction(Info.EntryName) != nullptr)
    fail(Kernel, Twine("generated entry-point name collides with '") +
                     Info.EntryName + "'");

  Function *Entry =
      Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                       GlobalValue::ExternalLinkage, Info.EntryName, M);
  Entry->setDSOLocal(true);
  Entry->addFnAttr("hlsl.shader", "compute");
  Entry->addFnAttr("hlsl.numthreads", "256,1,1");

  BasicBlock *Head = BasicBlock::Create(Context, "entry", Entry);
  BasicBlock *Run = BasicBlock::Create(Context, "run", Entry);
  BasicBlock *Done = BasicBlock::Create(Context, "done", Entry);
  IRBuilder<> Builder(Head);
  Value *Gid32 =
      Builder.CreateIntrinsic(Builder.getInt32Ty(), Intrinsic::spv_thread_id,
                              {Builder.getInt32(0)}, nullptr, "vthomas.gid.x");
  Value *Gid64 = Builder.CreateZExt(Gid32, Builder.getInt64Ty(), "vthomas.gid");
  Value *InvocationCount = loadPushI64(Builder, PushConstants, BlockType, 0);
  Builder.CreateCondBr(
      Builder.CreateICmpULT(Gid64, InvocationCount, "vthomas.in_range"), Run,
      Done);

  Builder.SetInsertPoint(Run);
  SmallVector<Value *> CallArguments;
  SmallVector<BufferPointerLowering> BufferPointers;
  Type *IndexType = Kernel.getFunctionType()->getParamType(0);
  CallArguments.push_back(IndexType->isIntegerTy(64)
                              ? Gid64
                              : Builder.CreateTrunc(Gid64, IndexType));

  unsigned Binding = 0;
  for (unsigned I = 1; I < Info.Arguments.size(); ++I) {
    const SignatureArgument &Argument = Info.Arguments[I];
    if (Argument.Role == ArgumentRole::Scalar) {
      CallArguments.push_back(
          loadScalar(Builder, Kernel.getFunctionType()->getParamType(I),
                     PushConstants, BlockType, Info.WordOffsets[I]));
      continue;
    }
    Value *ByteOffset =
        loadPushI64(Builder, PushConstants, BlockType, Info.WordOffsets[I]);
    BufferPointerLowering Buffer =
        createBufferPointer(Builder, M, Argument, Binding++, ByteOffset, DL,
                            (Info.SourceName + ".arg" + std::to_string(I)));
    CallArguments.push_back(Buffer.Pointer);
    BufferPointers.push_back(Buffer);
  }

  Kernel.removeFnAttr(Attribute::NoInline);
  Kernel.removeFnAttr(Attribute::OptimizeNone);
  Kernel.setCallingConv(CallingConv::SPIR_FUNC);
  CallInst *Call = Builder.CreateCall(&Kernel, CallArguments);
  Call->setCallingConv(CallingConv::SPIR_FUNC);
  Builder.CreateBr(Done);
  Builder.SetInsertPoint(Done);
  Builder.CreateRetVoid();

  InlineFunctionInfo IFI;
  InlineResult Inlined = InlineFunction(*Call, IFI);
  if (!Inlined.isSuccess())
    fail(Kernel, Twine("could not inline the kernel body: ") +
                     Inlined.getFailureReason());
  lowerTopLevelBufferGEPs(Kernel, BufferPointers);
  diagnoseUnsupportedPointerTransport(Kernel, *Entry);
  return Entry;
}

Metadata *mdUInt(LLVMContext &Context, unsigned Value) {
  return ConstantAsMetadata::get(
      ConstantInt::get(Type::getInt32Ty(Context), Value));
}

void emitReflection(Module &M, const KernelInfo &Info) {
  LLVMContext &Context = M.getContext();
  SmallVector<Metadata *> Arguments;
  unsigned Binding = 0;
  for (unsigned I = 1; I < Info.Arguments.size(); ++I) {
    const SignatureArgument &Argument = Info.Arguments[I];
    const bool IsBuffer = Argument.Role != ArgumentRole::Scalar;
    Arguments.push_back(MDNode::get(
        Context,
        {MDString::get(Context, IsBuffer ? "buffer" : "scalar"),
         mdUInt(Context, I),
         MDString::get(Context, typeCodeName(Argument.TypeCode)),
         mdUInt(Context, Info.WordOffsets[I]),
         mdUInt(Context, IsBuffer ? 2
                                  : divideCeil(Info.Kernel->getFunctionType()
                                                   ->getParamType(I)
                                                   ->getPrimitiveSizeInBits(),
                                               uint64_t{32})),
         mdUInt(Context, 0), mdUInt(Context, IsBuffer ? Binding++ : 0),
         MDString::get(Context, Argument.Role == ArgumentRole::ReadOnlyBuffer
                                    ? "read_only"
                                : IsBuffer ? "read_write"
                                           : "value")}));
  }
  MDNode *ArgumentList = MDNode::get(Context, Arguments);
  M.getOrInsertNamedMetadata(ReflectionMetadata)
      ->addOperand(MDNode::get(
          Context, {MDString::get(Context, Info.SourceName),
                    MDString::get(Context, Info.EntryName),
                    mdUInt(Context, WorkgroupSize),
                    mdUInt(Context, Info.PushConstantWords * sizeof(uint32_t)),
                    ArgumentList}));
}

class VThomasLowerPass : public PassInfoMixin<VThomasLowerPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    SmallPtrSet<Function *, 8> Discovered = discoverKernels(M);
    if (Discovered.empty())
      return PreservedAnalyses::all();

    SmallVector<KernelInfo> Kernels = readKernelSignatures(M, Discovered);
    unsigned MaxPushConstantWords = 0;
    for (KernelInfo &Info : Kernels) {
      validateAndLayoutKernel(Info, M.getDataLayout());
      MaxPushConstantWords =
          std::max(MaxPushConstantWords, Info.PushConstantWords);
    }

    StructType *PushConstantType = nullptr;
    GlobalVariable *PushConstants =
        createPushConstantBlock(M, MaxPushConstantWords, PushConstantType);
    for (KernelInfo &Info : Kernels) {
      createEntryPoint(Info, M, PushConstants, PushConstantType,
                       M.getDataLayout());
      emitReflection(M, Info);
      Info.Kernel->removeDeadConstantUsers();
      if (!Info.Kernel->use_empty()) {
        std::string Uses;
        raw_string_ostream Stream(Uses);
        for (User *User : Info.Kernel->users()) {
          Stream << "\n  ";
          User->print(Stream);
        }
        fail(*Info.Kernel,
             "remains referenced after its body was inlined into the entry "
             "point:" +
                 Uses);
      }
      Info.Kernel->eraseFromParent();
    }

    SmallVector<GlobalVariable *> DeadSourceMetadata;
    for (GlobalVariable &GV : M.globals()) {
      GV.removeDeadConstantUsers();
      if (GV.use_empty() && GV.hasLocalLinkage() &&
          GV.getSection() == "llvm.metadata")
        DeadSourceMetadata.push_back(&GV);
    }
    for (GlobalVariable *GV : DeadSourceMetadata)
      GV->eraseFromParent();

    if (verifyModule(M, &errs()))
      report_fatal_error("VTHOMAS generated invalid LLVM IR");
    return PreservedAnalyses::none();
  }
};

void registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != "vthomas-lower")
          return false;
        MPM.addPass(VThomasLowerPass());
        return true;
      });
}

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "VThomasLower", LLVM_VERSION_STRING,
          registerPassBuilderCallbacks, nullptr};
}
