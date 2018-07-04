//===- lib/Trace/ThreadStateMover.cpp -------------------------------------===//
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
#include "seec/Trace/ProcessState.hpp"
#include "seec/Trace/ThreadState.hpp"
#include "seec/Trace/TraceFormat.hpp"
#include "seec/Trace/TraceReader.hpp"
#include "seec/Trace/TraceSearch.hpp"
#include "seec/Util/ModuleIndex.hpp"
#include "seec/Util/Reverse.hpp"

#include "ThreadStateMover.hpp"

#include <cassert>

namespace seec {
namespace trace {


struct ThreadStateMoverImpl {
  ThreadState &m_State;
  
  EventReference const &m_EvRef;
  
  ThreadStateMoverImpl(ThreadState &State,
                       EventReference const &EvRef)
  : m_State(State),
    m_EvRef(EvRef)
  {}
  
  void setThreadTime(uint64_t const ThreadTime);
  void incrementThreadTime();
  void decrementThreadTime();
  
  void addEvent(EventRecord<EventType::None> const &);
  void addEvent(EventRecord<EventType::FunctionStart> const &);
  void addEvent(EventRecord<EventType::FunctionEnd> const &);
  void addEvent(EventRecord<EventType::NewProcessTime> const &);
  void addEvent(EventRecord<EventType::NewThreadTime> const &);
  void addEvent(EventRecord<EventType::PreInstruction> const &);
  void addEvent(EventRecord<EventType::Instruction> const &);
  void addEvent(EventRecord<EventType::InstructionWithUInt8> const &);
  void addEvent(EventRecord<EventType::InstructionWithUInt16> const &);
  void addEvent(EventRecord<EventType::InstructionWithUInt32> const &);
  void addEvent(EventRecord<EventType::InstructionWithUInt64> const &);
  void addEvent(EventRecord<EventType::InstructionWithPtr> const &);
  void addEvent(EventRecord<EventType::InstructionWithFloat> const &);
  void addEvent(EventRecord<EventType::InstructionWithDouble> const &);
  void addEvent(EventRecord<EventType::InstructionWithLongDouble> const &);
  void addEvent(EventRecord<EventType::StackRestore> const &);
  void addEvent(EventRecord<EventType::Alloca> const &);
  void addEvent(EventRecord<EventType::Malloc> const &);
  void addEvent(EventRecord<EventType::Free> const &);
  void addEvent(EventRecord<EventType::Realloc> const &);
  void addEvent(EventRecord<EventType::StateUntypedSmall> const &);
  void addEvent(EventRecord<EventType::StateUntyped> const &);
  void addEvent(EventRecord<EventType::StateMemmove> const &);
  void addEvent(EventRecord<EventType::StateClear> const &);
  void addEvent(EventRecord<EventType::KnownRegionAdd> const &);
  void addEvent(EventRecord<EventType::KnownRegionRemove> const &);
  void addEvent(EventRecord<EventType::ByValRegionAdd> const &);
  void addEvent(EventRecord<EventType::FileOpen> const &);
  void addEvent(EventRecord<EventType::FileWrite> const &);
  void addEvent(EventRecord<EventType::FileWriteFromMemory> const &);
  void addEvent(EventRecord<EventType::FileClose> const &);
  void addEvent(EventRecord<EventType::DirOpen> const &);
  void addEvent(EventRecord<EventType::DirClose> const &);
  void addEvent(EventRecord<EventType::RuntimeError> const &);

  void makePreviousInstructionActive(EventReference PriorTo);
  void setPreviousViewOfProcessTime(EventReference PriorTo);

  void removeEvent(EventRecord<EventType::None> const &);
  void removeEvent(EventRecord<EventType::FunctionStart> const &);
  void removeEvent(EventRecord<EventType::FunctionEnd> const &);
  void removeEvent(EventRecord<EventType::NewProcessTime> const &);
  void removeEvent(EventRecord<EventType::NewThreadTime> const &);
  void removeEvent(EventRecord<EventType::PreInstruction> const &);
  void removeEvent(EventRecord<EventType::Instruction> const &);
  void removeEvent(EventRecord<EventType::InstructionWithUInt8> const &);
  void removeEvent(EventRecord<EventType::InstructionWithUInt16> const &);
  void removeEvent(EventRecord<EventType::InstructionWithUInt32> const &);
  void removeEvent(EventRecord<EventType::InstructionWithUInt64> const &);
  void removeEvent(EventRecord<EventType::InstructionWithPtr> const &);
  void removeEvent(EventRecord<EventType::InstructionWithFloat> const &);
  void removeEvent(EventRecord<EventType::InstructionWithDouble> const &);
  void removeEvent(EventRecord<EventType::InstructionWithLongDouble> const &);
  void removeEvent(EventRecord<EventType::StackRestore> const &);
  void removeEvent(EventRecord<EventType::Alloca> const &);
  void removeEvent(EventRecord<EventType::Malloc> const &);
  void removeEvent(EventRecord<EventType::Free> const &);
  void removeEvent(EventRecord<EventType::Realloc> const &);
  void removeEvent(EventRecord<EventType::StateUntypedSmall> const &);
  void removeEvent(EventRecord<EventType::StateUntyped> const &);
  void removeEvent(EventRecord<EventType::StateMemmove> const &);
  void removeEvent(EventRecord<EventType::StateClear> const &);
  void removeEvent(EventRecord<EventType::KnownRegionAdd> const &);
  void removeEvent(EventRecord<EventType::KnownRegionRemove> const &);
  void removeEvent(EventRecord<EventType::ByValRegionAdd> const &);
  void removeEvent(EventRecord<EventType::FileOpen> const &);
  void removeEvent(EventRecord<EventType::FileWrite> const &);
  void removeEvent(EventRecord<EventType::FileWriteFromMemory> const &);
  void removeEvent(EventRecord<EventType::FileClose> const &);
  void removeEvent(EventRecord<EventType::DirOpen> const &);
  void removeEvent(EventRecord<EventType::DirClose> const &);
  void removeEvent(EventRecord<EventType::RuntimeError> const &);
};


/// \brief This handles dispatching to the various addEvent / removeEvent
/// methods, using SFINAE to avoid attempting to call adders or removers
/// for subservient events.
///
class ThreadMovementDispatcher {
public:
  /// \brief Adder for non-subservient events (forwards to Thread.addEvent).
  ///
  template<EventType ET>
  static
  typename std::enable_if< !is_subservient<ET>::value >::type
  addNextEventForwarder(ThreadState &Thread, EventReference const &Event)
  {
    assert(!is_function_level<ET>::value || !Thread.getCallStack().empty());
    ThreadStateMoverImpl(Thread, Event).addEvent(Event.get<ET>());
  }
  
  /// \brief Added for subservient events (adds nothing).
  ///
  template<EventType ET>
  static
  typename std::enable_if< is_subservient<ET>::value >::type
  addNextEventForwarder(ThreadState &Thread, EventReference const &Event)
  {
    assert(!is_function_level<ET>::value || !Thread.getCallStack().empty());
  }
  
  /// \brief Remover for non-subservient events.
  ///
  template<EventType ET>
  static
  typename std::enable_if< !is_subservient<ET>::value >::type
  removePreviousEventForwarder(ThreadState &Thread, EventReference const &Event)
  {
    ThreadStateMoverImpl(Thread, Event).removeEvent(Event.get<ET>());
  }
  
  /// \brief Remover for subservient events (removes nothing).
  ///
  template<EventType ET>
  static
  typename std::enable_if< is_subservient<ET>::value >::type
  removePreviousEventForwarder(ThreadState &Thread, EventReference const &Event)
  {}
};

//------------------------------------------------------------------------------
// Adding events
//------------------------------------------------------------------------------

void ThreadStateMoverImpl::setThreadTime(uint64_t const ThreadTime)
{
  m_State.ThreadTime = ThreadTime;

  auto Signals = m_State.getTrace().getCaughtSignalsAtTime(ThreadTime);
  if (Signals) {
    m_State.m_CaughtSignals = std::move(*Signals);
  }
  else {
    m_State.m_CaughtSignals.clear();
  }
}

void ThreadStateMoverImpl::incrementThreadTime()
{
  setThreadTime(m_State.ThreadTime + 1);
}

void ThreadStateMoverImpl::decrementThreadTime()
{
  setThreadTime(m_State.ThreadTime - 1);
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::None> const &Ev) {}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::FunctionStart> const &Ev)
{
  auto Info =
    llvm::make_unique<FunctionTrace>(m_State.Trace.getFunctionTrace(Ev));
  auto const Index = Info->getIndex();
  
  auto const MappedFunction =
    m_State.Parent.getModule().getFunctionIndex(Index);
  assert(MappedFunction && "Couldn't get FunctionIndex");

  auto State = llvm::make_unique<FunctionState>
                (m_State,
                 Index,
                 *MappedFunction,
                 m_State.Parent.getValueStoreModuleInfo(),
                 std::move(Info));
  assert(State);
  
  m_State.CallStack.emplace_back(std::move(State));
  
  setThreadTime(Ev.getThreadTimeEntered());
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::FunctionEnd> const &Ev)
{
  auto &Parent = m_State.Parent;
  auto &CallStack = m_State.CallStack;
  
  auto const &StartEv = Parent.getTrace()
                              .getEventAtOffset<EventType::FunctionStart>
                                               (Ev.getEventOffsetStart());
  
  auto const Index = StartEv.getFunctionIndex();

  assert(CallStack.size() && "FunctionEnd with empty CallStack");
  assert(CallStack.back()->getIndex() == Index
         && "FunctionEnd does not match currently active function");

  // Remove stack allocations from the memory state.
  auto const &ByVals = CallStack.back()->getParamByValStates();
  for (auto const &ByVal : ByVals)
    Parent.Memory.allocationRemove(ByVal.getArea().address(),
                                   ByVal.getArea().length());

  auto const &Allocas = CallStack.back()->getAllocas();
  for (auto const &Alloca : Allocas)
    Parent.Memory.allocationRemove(Alloca.getAddress(), Alloca.getTotalSize());

  m_State.m_CompletedFunctions.emplace_back(std::move(CallStack.back()));
  CallStack.pop_back();
  
  setThreadTime(StartEv.getThreadTimeExited());
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::NewProcessTime> const &Ev)
{
  // Update this thread's view of ProcessTime.
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::NewThreadTime> const &Ev)
{
  incrementThreadTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::PreInstruction> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setActiveInstructionIncomplete(Index);
  
  incrementThreadTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::Instruction> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithUInt8> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValueUInt64(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithUInt16> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValueUInt64(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithUInt32> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValueUInt64(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithUInt64> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValueUInt64(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithPtr> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValuePtr(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithFloat> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValueFloat(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithDouble> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  FuncState.setValueDouble(FuncState.getInstruction(Index), Ev.getValue());
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void ThreadStateMoverImpl::addEvent(
      EventRecord<EventType::InstructionWithLongDouble> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const Index = Ev.getIndex();
  FuncState.forwardingToInstruction(Index);
  
  uint64_t const Words[2] = { Ev.getValueWord1(), Ev.getValueWord2() };
  auto const Instruction = FuncState.getInstruction(Index);
  auto const Type = Instruction->getType();
  
  if (Type->isX86_FP80Ty()) {
    FuncState.setValueAPFloat(Instruction,
                              llvm::APFloat(llvm::APFloat::x87DoubleExtended(),
                                            llvm::APInt(80, Words)));
  }
  else {
    llvm_unreachable("unhandled long double type");
  }
  
  FuncState.setActiveInstructionComplete(Index);
  
  incrementThreadTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::StackRestore> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  auto const Removed = FuncState.removeAllocas(Ev.getPopCount());

  // Remove allocations for all the cleared allocas.
  for (auto const &Alloca : Removed)
    m_State.Parent.Memory.allocationRemove(Alloca.getAddress(),
                                           Alloca.getTotalSize());
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::Alloca> const &Ev)
{
  assert(m_EvRef != m_State.Trace.events().begin() && "Malformed event trace");
  
  // Find the preceding InstructionWithPtr event.
  auto const MaybeInstrRef =
    rfind<EventType::InstructionWithPtr>
         (rangeBeforeIncluding(m_State.Trace.events(), m_EvRef));
  
  assert(MaybeInstrRef.assigned() && "Malformed event trace");
  
  auto const &InstrRef = MaybeInstrRef.get<0>();
  auto const &Instr = InstrRef.get<EventType::InstructionWithPtr>();
  
  // Add Alloca information.
  FunctionState &FuncState = *(m_State.CallStack.back());
  auto &Allocas = FuncState.getAllocas();
  
  Allocas.emplace_back(FuncState,
                       Instr.getIndex(),
                       Instr.getValue(),
                       Ev.getElementSize(),
                       Ev.getElementCount());

  auto &Alloca = Allocas.back();
  m_State.Parent.Memory.allocationAdd(Alloca.getAddress(),
                                      Alloca.getTotalSize());
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::Malloc> const &Ev)
{
  // Find the preceding InstructionWithPtr event.
  auto const MaybeInstrRef =
    rfind<EventType::InstructionWithPtr>
         (rangeBeforeIncluding(m_State.Trace.events(), m_EvRef));

  assert(MaybeInstrRef.assigned() && "Malformed event trace");

  auto const &InstrRef = MaybeInstrRef.get<0>();
  auto const &InstrEv = InstrRef.get<EventType::InstructionWithPtr>();

  auto const Address = InstrEv.getValue();
  
  llvm::Instruction const *Allocator = nullptr;
  if (!m_State.CallStack.empty())
    Allocator = m_State.CallStack.back()->getInstruction(InstrEv.getIndex());
  
  // Update the shared ProcessState.
  auto &Parent = m_State.Parent;
  
  Parent.addMalloc(Address, Ev.getSize(), Allocator);
  Parent.Memory.allocationAdd(Address, Ev.getSize());

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::Free> const &Ev)
{
  auto &Parent = m_State.Parent;
  
  // Find the original Malloc event.
  auto const Address = Ev.getAddress();
  auto const Size = Parent.Mallocs.find(Address)->second.getSize();

  // Update the shared ProcessState.
  Parent.removeMalloc(Address);
  Parent.Memory.allocationRemove(Address, Size);
  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::Realloc> const &Ev)
{
  auto &Parent = m_State.Parent;
  
  auto const It = Parent.Mallocs.find(Ev.getAddress());
  assert(It != Parent.Mallocs.end());

  It->second.pushAllocator(m_State.CallStack.back()->getActiveInstruction());
  It->second.setSize(Ev.getNewSize());
  Parent.Memory.allocationResize(Ev.getAddress(),
                                 Ev.getOldSize(),
                                 Ev.getNewSize());
  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::
  addEvent(EventRecord<EventType::StateUntypedSmall> const &Ev)
{
  auto &Parent = m_State.Parent;
  
  auto DataPtr = reinterpret_cast<char const *>(&(Ev.getData()));
  Parent.Memory.addBlock(MappedMemoryBlock(Ev.getAddress(),
                                           Ev.getSize(),
                                           DataPtr));
  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::StateUntyped> const &Ev)
{
  auto &Parent = m_State.Parent;
  
  auto Data = Parent.getTrace().getData(Ev.getDataOffset(), Ev.getDataSize());
  Parent.Memory.addBlock(MappedMemoryBlock(Ev.getAddress(),
                                           Ev.getDataSize(),
                                           Data.data()));
  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::StateMemmove> const &Ev)
{
  auto &Parent = m_State.Parent;
  
  Parent.Memory.addCopy(Ev.getSourceAddress(),
                        Ev.getDestinationAddress(),
                        Ev.getSize());
  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::StateClear> const &Ev)
{
  auto &Parent = m_State.Parent;
  Parent.Memory.addClear(MemoryArea(Ev.getAddress(), Ev.getClearSize()));
  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::KnownRegionAdd> const &Ev)
{
  auto const Access =
    Ev.getReadable() ? (Ev.getWritable() ? MemoryPermission::ReadWrite
                                         : MemoryPermission::ReadOnly)
                     : (Ev.getWritable() ? MemoryPermission::WriteOnly
                                         : MemoryPermission::None);
  
  auto &Parent = m_State.Parent;
  Parent.addKnownMemory(Ev.getAddress(), Ev.getSize(), Access);
  Parent.Memory.allocationAdd(Ev.getAddress(), Ev.getSize());
}

void
ThreadStateMoverImpl::
  addEvent(EventRecord<EventType::KnownRegionRemove> const &Ev)
{
  auto &Parent = m_State.Parent;
  Parent.removeKnownMemory(Ev.getAddress());
  Parent.Memory.allocationRemove(Ev.getAddress(), Ev.getSize());
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::ByValRegionAdd> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  FuncState.addByValArea(Ev.getArgument(), Ev.getAddress(), Ev.getSize());
  m_State.Parent.Memory.allocationAdd(Ev.getAddress(), Ev.getSize());
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::FileOpen> const &Ev)
{
  auto &Parent = m_State.getParent();
  auto const &Trace = Parent.getTrace();
  auto const Filename = Trace.getDataRaw(Ev.getFilenameOffset());
  auto const Mode = Trace.getDataRaw(Ev.getModeOffset());
  
  Parent.addStream(StreamState{Ev.getFileAddress(),
                               StreamState::StandardStreamKind::none,
                               std::string{Filename},
                               std::string{Mode}});

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::FileWrite> const &Ev)
{
  auto &Parent = m_State.getParent();
  auto const Stream = Parent.getStream(Ev.getFileAddress());
  assert(Stream && "FileWrite with unknown FILE!");
  
  Stream->write(Parent.getTrace().getData(Ev.getDataOffset(),
                                          Ev.getDataSize()));

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void
ThreadStateMoverImpl::addEvent(EventRecord<EventType::FileWriteFromMemory> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  auto const Stream = Parent.getStream(Ev.getFileAddress());
  assert(Stream && "FileWriteFromMemory with unknown FILE!");
  
  auto const Region = Parent.Memory.getRegion(MemoryArea(Ev.getDataAddress(),
                                                         Ev.getDataSize()));
  
  if (Ev.getDataSize()) {
    assert(Region.isCompletelyInitialized() &&
           "FileWriteFromMemory with invalid MemoryArea!");
  }
  
  Stream->write(Region.getByteValues());

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::FileClose> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  Parent.closeStream(Ev.getFileAddress());

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::DirOpen> const &Ev)
{
  auto &Parent = m_State.getParent();
  auto const &Trace = Parent.getTrace();
  auto const Dirname = Trace.getDataRaw(Ev.getDirnameOffset());
  
  Parent.addDir(DIRState{Ev.getDirAddress(), std::string{Dirname}});

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::DirClose> const &Ev)
{
  auto &Parent = m_State.getParent();
  Parent.removeDir(Ev.getDirAddress());

  Parent.ProcessTime = Ev.getProcessTime();
  m_State.ProcessTime = Ev.getProcessTime();
}

void ThreadStateMoverImpl::addEvent(EventRecord<EventType::RuntimeError> const &Ev) {
  if (!Ev.getIsTopLevel())
    return;
  
  auto &Trace = m_State.getTrace();
  
  auto MaybeEvRef = Trace.getThreadEventBlockSequence().getReferenceTo(Ev);
  assert(MaybeEvRef && "Malformed event trace");
  auto const &EvRef = *MaybeEvRef;
  
  auto ErrRange = rangeAfterIncluding(Trace.events(), EvRef);
  auto ReadError = deserializeRuntimeError(ErrRange);
  assert(ReadError && "Malformed trace file.");
  
  m_State.CallStack.back()->addRuntimeError(std::move(ReadError));
}

void addNextEvent(ThreadState &State)
{
  auto &NextEvent = State.getNextEvent();
  
  switch (NextEvent->getType()) {
#define SEEC_TRACE_EVENT(NAME, MEMBERS, TRAITS)                                \
    case EventType::NAME:                                                      \
      ThreadMovementDispatcher::addNextEventForwarder<EventType::NAME>         \
        (State, NextEvent);                                                    \
      break;
#include "seec/Trace/Events.def"
    default: llvm_unreachable("Reference to unknown event type!");
  }

  State.incrementNextEvent();
}


//------------------------------------------------------------------------------
// Removing events
//------------------------------------------------------------------------------

void ThreadStateMoverImpl::makePreviousInstructionActive(EventReference PriorTo)
{
  auto &FuncState = *(m_State.CallStack.back());

  // Find the previous instruction event that is part of the same function
  // invocation as PriorTo, if there is such an event.
  auto MaybeRef = rfindInFunction(m_State.getTrace(),
                                  rangeBefore(m_State.getTrace().events(),
                                              PriorTo),
                                  [](EventRecordBase const &Ev) -> bool {
                                    return Ev.isInstruction();
                                  });

  if (!MaybeRef.assigned()) {
    FuncState.clearActiveInstruction();
    return;
  }
  
  // Set the previous instruction as active.
  auto MaybeIndex = MaybeRef.get<0>()->getIndex();
  assert(MaybeIndex.hasValue());

  // Set the correct BasicBlocks to be active.
  FuncState.rewindingToInstruction(*MaybeIndex);
  
  if (MaybeRef.get<0>()->getType() != EventType::PreInstruction)
    FuncState.setActiveInstructionComplete(*MaybeIndex);
  else
    FuncState.setActiveInstructionIncomplete(*MaybeIndex);
  
  // Find all runtime errors attached to the previous instruction, and make
  // them active.
  auto ErrorSearchRange = EventRange(MaybeRef.get<0>(), PriorTo);
  
  for (auto Ev : ErrorSearchRange) {
    if (Ev.getType() != EventType::RuntimeError)
      continue;
    
    addEvent(Ev.as<EventType::RuntimeError>());
  }
}

void
ThreadStateMoverImpl::setPreviousViewOfProcessTime(EventReference PriorTo)
{
  // Find the previous event that sets the process time, if there is one.
  auto MaybeRef = rfind(rangeBefore(m_State.getTrace().events(), PriorTo),
                        [](EventRecordBase const &Ev) -> bool {
                          return Ev.getProcessTime().hasValue();
                        });
  
  if (!MaybeRef.assigned())
    m_State.ProcessTime = 0;
  else
    m_State.ProcessTime = *(MaybeRef.get<0>()->getProcessTime());
}

void ThreadStateMoverImpl::removeEvent(EventRecord<EventType::None> const &Ev)
{}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::FunctionStart> const &Ev)
{
  auto const Info = m_State.getTrace().getFunctionTrace(Ev);
  auto const Index = Info.getIndex();
  
  auto &CallStack = m_State.CallStack;

  assert(CallStack.size() && "Removing FunctionStart with empty CallStack");
  assert(CallStack.back()->getIndex() == Index
         && "Removing FunctionStart does not match currently active function");

  CallStack.pop_back();
  setThreadTime(Info.getThreadTimeEntered() - 1);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::FunctionEnd> const &Ev)
{
  auto &CallStack = m_State.CallStack;
  auto &Parent = m_State.getParent();
  
  CallStack.emplace_back(std::move(m_State.m_CompletedFunctions.back()));
  m_State.m_CompletedFunctions.pop_back();
  
  auto &StateRef = *(CallStack.back());
  
  // Restore alloca allocations (reverse order):
  for (auto const &Alloca : seec::reverse(StateRef.getAllocas()))
    Parent.Memory.allocationUnremove(Alloca.getAddress(),
                                     Alloca.getTotalSize());

  // Restore byval areas (reverse order):
  for (auto const &ByVal : seec::reverse(StateRef.getParamByValStates()))
    Parent.Memory.allocationUnremove(ByVal.getArea().address(),
                                     ByVal.getArea().length());

  // Set the thread time to the value that it had prior to this event.
  setThreadTime(StateRef.getTrace().getThreadTimeExited() - 1);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::NewProcessTime> const &Ev)
{
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::NewThreadTime> const &Ev)
{
  decrementThreadTime();
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::PreInstruction> const &Ev)
{
  makePreviousInstructionActive(m_EvRef);
  decrementThreadTime();
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::Instruction> const &Ev)
{
  makePreviousInstructionActive(m_EvRef);
  decrementThreadTime();
}

template<EventType ET>
EventRecord<ET> const *getPreviousSame(ThreadTrace const &Trace,
                                       EventRecord<ET> const &Ev)
{
  auto const Range = rangeBefore(Trace.events(), Ev);
  auto const MaybeRef =
    rfindInFunction(Trace, Range, [&Ev] (EventRecordBase const &Other) {
      return Other.getType() == ET
             && Other.as<ET>().getIndex() == Ev.getIndex();
    });

  if (MaybeRef.template assigned<EventReference>())
    return &(MaybeRef.template get<EventReference>().template get<ET>());

  return nullptr;
}

#define SEEC_IMPLEMENT_REMOVE_INSTRUCTION(TYPE)                                \
void                                                                           \
ThreadStateMoverImpl::                                                         \
  removeEvent(EventRecord<EventType::InstructionWith##TYPE> const &Ev)         \
{                                                                              \
  makePreviousInstructionActive(m_EvRef);                                      \
  decrementThreadTime();                                                       \
}

SEEC_IMPLEMENT_REMOVE_INSTRUCTION(UInt8)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(UInt16)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(UInt32)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(UInt64)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(Ptr)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(Float)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(Double)
SEEC_IMPLEMENT_REMOVE_INSTRUCTION(LongDouble)

#undef SEEC_IMPLEMENT_REMOVE_INSTRUCTION

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::StackRestore> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  
  auto const ClearCount = Ev.getPopCount();
  auto const Restored = FuncState.unremoveAllocas(ClearCount);
  
  auto ReverseRestored =
    range(std::reverse_iterator<decltype(Restored.end())>(Restored.end()),
          std::reverse_iterator<decltype(Restored.begin())>(Restored.begin()));
  
  // Restore allocations that have been unremoved. We have to do this in
  // reverse order so that MemoryState retrieves the correct data.
  for (auto const &Alloca : ReverseRestored) {
    m_State.getParent().Memory.allocationUnremove(Alloca.getAddress(),
                                                  Alloca.getTotalSize());
  }
}

void ThreadStateMoverImpl::removeEvent(EventRecord<EventType::Alloca> const &Ev)
{
  // Remove Alloca information.
  auto &Allocas = m_State.CallStack.back()->getAllocas();
  assert(!Allocas.empty());

  auto const &Alloca = Allocas.back();
  m_State.getParent().Memory.allocationUnadd(Alloca.getAddress(),
                                             Alloca.getTotalSize());

  Allocas.pop_back();
}

void ThreadStateMoverImpl::removeEvent(EventRecord<EventType::Malloc> const &Ev)
{
  // Find the preceding InstructionWithPtr event.
  auto const MaybeInstrRef =
    rfind<EventType::InstructionWithPtr>
         (rangeBeforeIncluding(m_State.getTrace().events(), m_EvRef));
  assert(MaybeInstrRef.assigned() && "Malformed event trace");

  auto const &InstrRef = MaybeInstrRef.get<0>();
  auto const &Instr = InstrRef.get<EventType::InstructionWithPtr>();
  auto const Address = Instr.getValue();

  auto &Parent = m_State.getParent();
  
  Parent.unaddMalloc(Address);
  Parent.Memory.allocationUnadd(Address, Ev.getSize());

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void ThreadStateMoverImpl::removeEvent(EventRecord<EventType::Free> const &Ev)
{
  // Information required to recreate the dynamic memory allocation.
  auto const Address = Ev.getAddress();
  auto &Parent = m_State.getParent();

  // Restore the dynamic memory allocation.
  Parent.unremoveMalloc(Address);

  // Restore the allocation in the memory state.
  auto const Size = Parent.Mallocs.find(Address)->second.getSize();
  Parent.Memory.allocationUnremove(Address, Size);

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::Realloc> const &Ev)
{
  auto &Parent = m_State.getParent();
  auto const It = Parent.Mallocs.find(Ev.getAddress());
  assert(It != Parent.Mallocs.end());

  It->second.setSize(Ev.getOldSize());
  Parent.Memory.allocationUnresize(Ev.getAddress(),
                                   Ev.getNewSize(),
                                   Ev.getOldSize());

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::StateUntypedSmall> const &Ev)
{
  auto &Parent = m_State.getParent();
  Parent.Memory.removeBlock(MemoryArea(Ev.getAddress(), Ev.getSize()));
  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::StateUntyped> const &Ev)
{
  auto &Parent = m_State.getParent();
  Parent.Memory.removeBlock(MemoryArea(Ev.getAddress(), Ev.getDataSize()));
  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::StateMemmove> const &Ev)
{
  auto &Parent = m_State.getParent();
  Parent.Memory.removeCopy(Ev.getSourceAddress(),
                           Ev.getDestinationAddress(),
                           Ev.getSize());
  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::StateClear> const &Ev)
{
  auto &Parent = m_State.getParent();
  Parent.Memory.removeClear(MemoryArea(Ev.getAddress(), Ev.getClearSize()));
  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::KnownRegionAdd> const &Ev)
{
  auto &Parent = m_State.getParent();
  Parent.removeKnownMemory(Ev.getAddress());
  Parent.Memory.allocationUnadd(Ev.getAddress(), Ev.getSize());
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::KnownRegionRemove> const &Ev)
{
  auto const Access =
    Ev.getReadable() ? (Ev.getWritable() ? MemoryPermission::ReadWrite
                                         : MemoryPermission::ReadOnly)
                     : (Ev.getWritable() ? MemoryPermission::WriteOnly
                                         : MemoryPermission::None);
  
  auto &Parent = m_State.getParent();
  Parent.addKnownMemory(Ev.getAddress(), Ev.getSize(), Access);
  Parent.Memory.allocationUnremove(Ev.getAddress(), Ev.getSize());
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::ByValRegionAdd> const &Ev)
{
  auto &FuncState = *(m_State.CallStack.back());
  FuncState.removeByValArea(Ev.getAddress());
  m_State.getParent().Memory.allocationUnadd(Ev.getAddress(), Ev.getSize());
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::FileOpen> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  Parent.removeStream(Ev.getFileAddress());

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::FileWrite> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  auto const Stream = Parent.getStream(Ev.getFileAddress());
  assert(Stream && "FileWrite with unknown FILE!");
  
  Stream->unwrite(Ev.getDataSize());

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::FileWriteFromMemory> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  auto const Stream = Parent.getStream(Ev.getFileAddress());
  assert(Stream && "FileWriteFromMemory with unknown FILE!");
  
  Stream->unwrite(Ev.getDataSize());

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::FileClose> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  auto const Restored = Parent.restoreStream(Ev.getFileAddress());
  assert(Restored && "Failed to restore FILE stream!");

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::DirOpen> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  Parent.removeDir(Ev.getDirAddress());

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::removeEvent(EventRecord<EventType::DirClose> const &Ev)
{
  auto &Parent = m_State.getParent();
  
  auto const &Trace = Parent.getTrace();
  auto const Dirname = Trace.getDataRaw(Ev.getDirnameOffset());
  
  Parent.addDir(DIRState{Ev.getDirAddress(), std::string{Dirname}});

  Parent.ProcessTime = Ev.getProcessTime() - 1;
  setPreviousViewOfProcessTime(m_EvRef);
}

void
ThreadStateMoverImpl::
  removeEvent(EventRecord<EventType::RuntimeError> const &Ev)
{
  if (!Ev.getIsTopLevel())
    return;
  
  m_State.CallStack.back()->removeLastRuntimeError();
}

void removePreviousEvent(ThreadState &State)
{
  State.decrementNextEvent();
  
  auto &NextEvent = State.getNextEvent();

  switch (NextEvent->getType()) {
#define SEEC_TRACE_EVENT(NAME, MEMBERS, TRAITS)                                \
    case EventType::NAME:                                                      \
      ThreadMovementDispatcher::removePreviousEventForwarder<EventType::NAME>  \
        (State, NextEvent);                                                    \
      break;
#include "seec/Trace/Events.def"
    default: llvm_unreachable("Reference to unknown event type!");
  }
}

} // namespace trace (in seec)
} // namespace seec
