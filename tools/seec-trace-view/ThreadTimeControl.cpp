//===- ThreadTimeControl.cpp ----------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#include "seec/ICU/Format.hpp"
#include "seec/ICU/Resources.hpp"
#include "seec/Trace/TraceFormat.hpp"
#include "seec/Trace/TraceReader.hpp"
#include "seec/Trace/TraceSearch.hpp"
#include "seec/wxWidgets/ImageResources.hpp"
#include "seec/wxWidgets/StringConversion.hpp"

#include "llvm/Support/raw_ostream.h"

#include <wx/bmpbuttn.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include "OpenTrace.hpp"
#include "ThreadTimeControl.hpp"


namespace seec {
  namespace trace {
    class ProcessState;
    class ThreadState;
  } // namespace trace (in seec)
} // namespace seec


IMPLEMENT_CLASS(ThreadTimeEvent, wxEvent)
wxDEFINE_EVENT(SEEC_EV_THREAD_TIME_CHANGED, ThreadTimeEvent);
wxDEFINE_EVENT(SEEC_EV_THREAD_TIME_VIEWED,  ThreadTimeEvent);

IMPLEMENT_DYNAMIC_CLASS(ThreadTimeControl, wxPanel);

enum ControlIDs {
  ControlID_Reset = wxID_HIGHEST,
  ControlID_ButtonGoToStart,
  ControlID_ButtonStepBack,
  ControlID_ButtonStepForward,
  ControlID_ButtonGoToNextError,
  ControlID_ButtonGoToEnd
};


//------------------------------------------------------------------------------
// event table
//------------------------------------------------------------------------------

BEGIN_EVENT_TABLE(ThreadTimeControl, wxPanel)
  EVT_BUTTON(ControlID_ButtonGoToStart, ThreadTimeControl::OnGoToStart)
  EVT_BUTTON(ControlID_ButtonStepBack, ThreadTimeControl::OnStepBack)
  EVT_BUTTON(ControlID_ButtonStepForward, ThreadTimeControl::OnStepForward)
  EVT_BUTTON(ControlID_ButtonGoToNextError, ThreadTimeControl::OnGoToNextError)
  EVT_BUTTON(ControlID_ButtonGoToEnd, ThreadTimeControl::OnGoToEnd)
END_EVENT_TABLE()


//------------------------------------------------------------------------------
// method implementations
//------------------------------------------------------------------------------

bool ThreadTimeControl::Create(wxWindow *Parent,
                               OpenTrace &TheTrace,
                               seec::trace::ThreadTrace const &TheThreadTrace,
                               wxWindowID ID) {
  if (!wxPanel::Create(Parent, ID))
    return false;

  Trace = &TheTrace;
  ThreadTrace = &TheThreadTrace;

  // Get the GUIText from the TraceViewer ICU resources.
  UErrorCode Status = U_ZERO_ERROR;
  auto TextTable = seec::getResource("TraceViewer",
                                     Locale::getDefault(),
                                     Status,
                                     "GUIText",
                                     "ScrollThreadTime");
  assert(U_SUCCESS(Status));

  // Get the GUI images from the TraceViewer ICU resources.
  auto ImageTable = seec::getResource("TraceViewer",
                                      Locale::getDefault(),
                                      Status,
                                      "GUIImages",
                                      "Movement");
  assert(U_SUCCESS(Status));

  // Create stepping buttons to control the thread time.
  auto ImageGoToStart = seec::getwxImageEx(ImageTable,
                                           "BackwardArrowToBlock",
                                           Status);
  auto ButtonGoToStart = new wxBitmapButton(this,
                                            ControlID_ButtonGoToStart,
                                            ImageGoToStart);

  auto ImageStepBack = seec::getwxImageEx(ImageTable,
                                          "BackwardArrow",
                                          Status);
  auto ButtonStepBack = new wxBitmapButton(this,
                                           ControlID_ButtonStepBack,
                                           ImageStepBack);

  auto ImageStepForward = seec::getwxImageEx(ImageTable,
                                             "ForwardArrow",
                                              Status);
  auto ButtonStepForward = new wxBitmapButton(this,
                                              ControlID_ButtonStepForward,
                                              ImageStepForward);

  auto ImageGoToNextError = seec::getwxImageEx(ImageTable,
                                               "ForwardArrowToError",
                                               Status);
  auto ButtonGoToNextError = new wxBitmapButton(this,
                                                ControlID_ButtonGoToNextError,
                                                ImageGoToNextError);

  auto ImageGoToEnd = seec::getwxImageEx(ImageTable,
                                         "ForwardArrowToBlock",
                                         Status);
  auto ButtonGoToEnd = new wxBitmapButton(this,
                                          ControlID_ButtonGoToEnd,
                                          ImageGoToEnd);

  // Position all of our controls.
  auto TopSizer = new wxBoxSizer(wxHORIZONTAL);
  TopSizer->Add(ButtonGoToStart, wxSizerFlags().Proportion(1).Shaped());
  TopSizer->Add(ButtonStepBack, wxSizerFlags().Proportion(1).Shaped());
  TopSizer->Add(ButtonStepForward, wxSizerFlags().Proportion(1).Shaped());
  TopSizer->Add(ButtonGoToNextError, wxSizerFlags().Proportion(1).Shaped());
  TopSizer->Add(ButtonGoToEnd, wxSizerFlags().Proportion(1).Shaped());
  TopSizer->SetSizeHints(this);
  SetSizer(TopSizer);

  return true;
}

void ThreadTimeControl::show(seec::trace::ProcessState &ProcessState,
                             seec::trace::ThreadState &ThreadState) {
  this->ThreadState = &ThreadState;

  // If the state's thread time doesn't match our thread time, we must update
  // ourself.

}

void ThreadTimeControl::OnGoToStart(wxCommandEvent &WXUNUSED(Event)) {
  if (ThreadState->getThreadTime() == 0)
    return;

  auto const ThreadID = ThreadTrace->getThreadID();
  ThreadTimeEvent Ev(SEEC_EV_THREAD_TIME_CHANGED, GetId(), ThreadID, 0);
  Ev.SetEventObject(this);
  ProcessWindowEvent(Ev);
}

void ThreadTimeControl::OnStepBack(wxCommandEvent &WXUNUSED(Event)) {
  if (ThreadState->getThreadTime() == 0)
    return;

  auto const Time = ThreadState->getThreadTime() - 1;

  auto const ThreadID = ThreadTrace->getThreadID();
  ThreadTimeEvent Ev(SEEC_EV_THREAD_TIME_CHANGED, GetId(), ThreadID, Time);
  Ev.SetEventObject(this);
  ProcessWindowEvent(Ev);
}

void ThreadTimeControl::OnStepForward(wxCommandEvent &WXUNUSED(Event)) {
  auto const MaxValue = ThreadTrace->getFinalThreadTime();

  if (ThreadState->getThreadTime() == MaxValue)
    return;

  auto const Time = ThreadState->getThreadTime() + 1;

  auto const ThreadID = ThreadTrace->getThreadID();
  ThreadTimeEvent Ev(SEEC_EV_THREAD_TIME_CHANGED, GetId(), ThreadID, Time);
  Ev.SetEventObject(this);
  ProcessWindowEvent(Ev);
}

void ThreadTimeControl::OnGoToNextError(wxCommandEvent &WXUNUSED(Event)) {
  if (ThreadState->getThreadTime() == ThreadTrace->getFinalThreadTime())
    return;

  assert(ThreadState);

  auto NextEv = ThreadState->getNextEvent();
  auto SearchRange = seec::trace::rangeAfterIncluding(ThreadTrace->events(),
                                                      NextEv);

  // Find the first RuntimeError in SearchRange.
  auto MaybeEvRef = seec::trace::find<seec::trace::EventType::RuntimeError>
                                     (SearchRange);
  if (!MaybeEvRef.assigned()) {
    // There are no more errors.
    return;
  }

  // Find the thread time at this position.
  auto TimeSearchRange = seec::trace::rangeBefore(ThreadTrace->events(),
                                                  MaybeEvRef.get<0>());

  auto LastEventTime = seec::trace::lastSuccessfulApply(
                                    TimeSearchRange,
                                    [](seec::trace::EventRecordBase const &Ev){
                                      return Ev.getThreadTime();
                                    });

  uint64_t LastTime = LastEventTime.assigned() ? LastEventTime.get<0>() : 0;

  auto const ThreadID = ThreadTrace->getThreadID();
  ThreadTimeEvent Ev(SEEC_EV_THREAD_TIME_CHANGED, GetId(), ThreadID, LastTime);
  Ev.SetEventObject(this);
  ProcessWindowEvent(Ev);
}

void ThreadTimeControl::OnGoToEnd(wxCommandEvent &WXUNUSED(Event)) {
  auto const MaxValue = ThreadTrace->getFinalThreadTime();

  if (ThreadState->getThreadTime() == MaxValue)
    return;

  auto const ThreadID = ThreadTrace->getThreadID();
  ThreadTimeEvent Ev(SEEC_EV_THREAD_TIME_CHANGED, GetId(), ThreadID, MaxValue);
  Ev.SetEventObject(this);
  ProcessWindowEvent(Ev);
}
