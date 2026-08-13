#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"

using namespace llvm;

namespace {

constexpr StringLiteral KernelAnnotation = "vthomas.kernel";

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

class VThomasLowerPass : public PassInfoMixin<VThomasLowerPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    GlobalVariable *Annotations = M.getGlobalVariable("llvm.global.annotations");
    if (Annotations == nullptr || !Annotations->hasInitializer())
      return PreservedAnalyses::all();

    const auto *Entries =
        dyn_cast<ConstantArray>(Annotations->getInitializer());
    if (Entries == nullptr)
      return PreservedAnalyses::all();

    bool Changed = false;
    for (const Use &EntryUse : Entries->operands()) {
      const auto *Entry = dyn_cast<ConstantStruct>(EntryUse.get());
      if (Entry == nullptr || Entry->getNumOperands() < 2)
        continue;
      if (getConstantCString(Entry->getOperand(1)) != KernelAnnotation)
        continue;

      auto *Kernel = dyn_cast<Function>(
          Entry->getOperand(0)->stripPointerCasts());
      if (Kernel == nullptr || Kernel->hasFnAttribute(KernelAnnotation))
        continue;

      // Normalizing source annotations into a function attribute gives later
      // ABI-lowering stages a stable discovery mechanism. Wrapper generation
      // and signature validation will be added at this seam.
      Kernel->addFnAttr(KernelAnnotation);
      Changed = true;
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
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

