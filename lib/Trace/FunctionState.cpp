//===- lib/Trace/FunctionState.cpp ----------------------------------------===//
//
//                                    SeeC
//
// This file is distributed under The MIT License (MIT). See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#include "seec/Trace/FunctionState.hpp"
#include "seec/Trace/MemoryState.hpp"
#include "seec/Trace/ThreadState.hpp"
#include "seec/Trace/ProcessState.hpp"
#include "seec/Util/ModuleIndex.hpp"

#include "llvm/Support/raw_ostream.h"

namespace seec {

namespace trace {


//===------------------------------------------------------------------------===
// AllocaState
//===------------------------------------------------------------------------===

llvm::AllocaInst const *AllocaState::getInstruction() const {
  auto &Lookup = Parent->getFunctionLookup();
  auto Inst = Lookup.getInstruction(InstructionIndex);
  assert(Inst && llvm::isa<llvm::AllocaInst>(Inst));
  return llvm::cast<llvm::AllocaInst>(Inst);
}

MemoryState::Region AllocaState::getMemoryRegion() const {
  auto &Thread = Parent->getParent();
  auto &Process = Thread.getParent();
  auto &Memory = Process.getMemory();
  return Memory.getRegion(MemoryArea(Address, getTotalSize()));
}


//===------------------------------------------------------------------------===
// FunctionState
//===------------------------------------------------------------------------===

FunctionState::FunctionState(ThreadState &Parent,
                             uint32_t Index,
                             FunctionIndex const &Function,
                             FunctionTrace Trace)
: Parent(&Parent),
  FunctionLookup(Parent.getParent().getModule().getFunctionIndex(Index)),
  Index(Index),
  Trace(Trace),
  ActiveInstruction(),
  InstructionValues(Function.getInstructionCount()),
  Allocas()
{
  assert(FunctionLookup);
}

llvm::Function const *FunctionState::getFunction() const {
  return Parent->getParent().getModule().getFunction(Index);
}

llvm::Instruction const *FunctionState::getInstruction(uint32_t Index) const {
  if (Index >= InstructionValues.size())
    return nullptr;
  
  return FunctionLookup->getInstruction(Index);
}

llvm::Instruction const *FunctionState::getActiveInstruction() const {
  if (!ActiveInstruction.assigned())
    return nullptr;

  return FunctionLookup->getInstruction(ActiveInstruction.get<0>());
}

RuntimeValue &FunctionState::getRuntimeValue(llvm::Instruction const *I) {
  auto MaybeIndex = FunctionLookup->getIndexOfInstruction(I);
  return getRuntimeValue(MaybeIndex.get<0>());
}

RuntimeValue const &
FunctionState::getRuntimeValue(llvm::Instruction const *I) const {
  auto MaybeIndex = FunctionLookup->getIndexOfInstruction(I);
  return getRuntimeValue(MaybeIndex.get<0>());
}


/// Print a textual description of a FunctionState.
llvm::raw_ostream &operator<<(llvm::raw_ostream &Out,
                              FunctionState const &State) {
  Out << "  Function [Index=" << State.getIndex() << "]\n";

  Out << "   Allocas:\n";
  for (auto const &Alloca : State.getAllocas()) {
    Out << "    " << Alloca.getInstructionIndex()
        <<  " =[" << Alloca.getElementCount()
        <<    "x" << Alloca.getElementSize()
        <<  "] @" << Alloca.getAddress()
        << "\n";
  }

  Out << "   Instruction values:\n";
  auto const InstructionCount = State.getInstructionCount();
  for (std::size_t i = 0; i < InstructionCount; ++i) {
    auto const &Value = State.getRuntimeValue(i);
    if (Value.assigned()) {
      auto Type = State.getInstruction(i)->getType();
      
      Out << "    " << i << " = ";
      
      if (llvm::isa<llvm::IntegerType>(Type)) {
        Out << "(int64_t)" << State.getRuntimeValueAs<int64_t>(i).get<0>()
            << ", (uint64_t)" << State.getRuntimeValueAs<uint64_t>(i).get<0>();
      }
      else if (Type->isFloatTy()) {
        Out << "(float)" << State.getRuntimeValueAs<float>(i).get<0>();
      }
      else if (Type->isDoubleTy()) {
        Out << "(double)" << State.getRuntimeValueAs<double>(i).get<0>();
      }
      else if (Type->isPointerTy()) {
        Out << "(? *)" << State.getRuntimeValueAs<void *>(i).get<0>();
      }
      else {
        Out << "(unknown type)";
      }
      
      Out << "\n";
    }
  }

  return Out;
}


} // namespace trace (in seec)

} // namespace seec
