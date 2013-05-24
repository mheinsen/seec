//===- tools/seec-trace-view/ThreadTimeControl.hpp ------------------------===//
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

#ifndef SEEC_TRACE_VIEW_THREADTIMECONTROL_HPP
#define SEEC_TRACE_VIEW_THREADTIMECONTROL_HPP


#include <wx/wx.h>
#include <wx/panel.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include <memory>


// Forward-declarations.
class StateAccessToken;


/// \brief Represents events requesting thread movement.
///
class ThreadMoveEvent : public wxEvent
{
public:
  enum class DirectionTy {
    Forward,
    Backward
  };

private:
  /// The thread associated with this event.
  size_t ThreadIndex;
  
  /// The direction to move the thread.
  DirectionTy Direction; 

public:
  // Make this class known to wxWidgets' class hierarchy.
  wxDECLARE_CLASS(ThreadMoveEvent);

  /// \brief Constructor.
  ///
  ThreadMoveEvent(wxEventType EventType,
                  int WinID,
                  size_t ForThreadIndex,
                  DirectionTy WithDirection)
  : wxEvent(WinID, EventType),
    ThreadIndex(ForThreadIndex),
    Direction(WithDirection)
  {
    this->m_propagationLevel = wxEVENT_PROPAGATE_MAX;
  }

  /// \brief Copy constructor.
  ///
  ThreadMoveEvent(ThreadMoveEvent const &Ev)
  : wxEvent(Ev),
    ThreadIndex(Ev.ThreadIndex),
    Direction(Ev.Direction)
  {
    this->m_propagationLevel = Ev.m_propagationLevel;
  }

  /// \brief wxEvent::Clone().
  ///
  virtual wxEvent *Clone() const {
    return new ThreadMoveEvent(*this);
  }

  /// \name Accessors
  /// @{
  
  size_t getThreadIndex() const { return ThreadIndex; }
  
  DirectionTy getDirection() const { return Direction; }
  
  /// @}
};

// Produced when the user changes the thread time.
wxDECLARE_EVENT(SEEC_EV_THREAD_MOVE, ThreadMoveEvent);

/// Used inside an event table to catch SEEC_EV_THREAD_MOVE.
#define SEEC_EVT_THREAD_MOVE(id, func) \
  wx__DECLARE_EVT1(SEEC_EV_THREAD_MOVE, id, (&func))


/// \brief A control that allows the user to navigate through the ThreadTime.
///
class ThreadTimeControl : public wxPanel
{
  std::shared_ptr<StateAccessToken> CurrentAccess;
  
  size_t CurrentThreadIndex;
  
public:
  // Make this class known to wxWidgets' class hierarchy, and dynamically
  // creatable.
  DECLARE_DYNAMIC_CLASS(ThreadTimeControl)

  /// \brief Constructor (without creation).
  /// A ThreadTimeControl constructed with this constructor must later be
  /// created by calling Create().
  ///
  ThreadTimeControl()
  : wxPanel(),
    CurrentAccess(),
    CurrentThreadIndex()
  {}

  /// \brief Constructor (with creation).
  ///
  ThreadTimeControl(wxWindow *Parent,
                    wxWindowID ID = wxID_ANY)
  : wxPanel(),
    CurrentAccess(),
    CurrentThreadIndex()
  {
    Create(Parent, ID);
  }

  /// \brief Create this object (if it was not created by the constructor).
  ///
  bool Create(wxWindow *Parent,
              wxWindowID ID = wxID_ANY);
  
  /// \brief Destructor.
  ///
  virtual ~ThreadTimeControl();

  /// \brief Update this control to reflect the given state.
  ///
  void show(std::shared_ptr<StateAccessToken> Access,
            size_t ThreadIndex);

  /// \name Event Handlers
  /// @{

  /// \brief Called when the GoToStart button is clicked.
  ///
  void OnGoToStart(wxCommandEvent &Event);

  /// \brief Called when the StepBack button is clicked.
  ///
  void OnStepBack(wxCommandEvent &Event);

  /// \brief Called when the StepForward button is clicked.
  ///
  void OnStepForward(wxCommandEvent &Event);

  /// \brief Called when the GoToNextError button is clicked.
  ///
  void OnGoToNextError(wxCommandEvent &Event);

  /// \brief Called when the GoToEnd button is clicked.
  ///
  void OnGoToEnd(wxCommandEvent &Event);

  /// @} (Event Handlers)

private:
  // Declare the static event table (it is defined in ThreadTimeControl.cpp)
  DECLARE_EVENT_TABLE();
};


#endif // SEEC_TRACE_VIEW_THREADTIMECONTROL_HPP
