//===- tools/seec-trace-view/TraceViewerApp.hpp ---------------------------===//
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

#ifndef SEEC_TRACE_VIEW_TRACEVIEWERAPP_HPP
#define SEEC_TRACE_VIEW_TRACEVIEWERAPP_HPP

#include "seec/ICU/Resources.hpp"

#include <wx/wx.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include <memory>
#include <set>
#include <vector>

//------------------------------------------------------------------------------
// TraceViewerApp
//------------------------------------------------------------------------------

class ActionRecordingSubmitter;
class SingleInstanceServer;
class WelcomeFrame;
class wxSingleInstanceChecker;

/// \brief The application class for the SeeC Trace Viewer.
///
class TraceViewerApp : public wxApp
{
  /// Ensures that no user can simultaneously run multiple trace viewers.
  std::unique_ptr<wxSingleInstanceChecker> SingleInstanceChecker;

  /// Receive notifications from other instances of the trace viewer.
  std::unique_ptr<SingleInstanceServer> Server;

  /// The welcome frame that is displayed when no files are open.
  WelcomeFrame *Welcome;

  /// All other top-level windows.
  std::set<wxWindow *> TopLevelWindows;

  /// The log window.
  wxLogWindow *LogWindow;

  /// Holds the ICU resource files used by this application.
  std::unique_ptr<seec::ResourceLoader> ICUResources;

  /// Files that the user passed on the command line.
  std::vector<wxString> CLFiles;

  /// True iff curl was initialized without error.
  bool const CURL;

  /// Handles submission of user action recordings.
  std::unique_ptr<ActionRecordingSubmitter> RecordingSubmitter;

  /// \brief Send any "files to open" to the existing trace viewer instance.
  ///
  void deferToExistingInstance();

  /// \brief Open a new trace viewer for the given file.
  ///
  void OpenFile(wxString const &FileName);

public:
  /// \brief Constructor.
  ///
  TraceViewerApp();

  /// \brief Destructor.
  ///
  virtual ~TraceViewerApp();

  /// \name Interface to wxApp.
  /// @{

  virtual bool OnInit();
  
  virtual void OnInitCmdLine(wxCmdLineParser &Parser);
  
  virtual bool OnCmdLineParsed(wxCmdLineParser &Parser);

  /// @}


  /// \name Mac OS X functionality
  /// @{

  virtual void MacNewFile();

  virtual void MacOpenFiles(wxArrayString const &FileNames);

  virtual void MacOpenFile(wxString const &FileName);

  virtual void MacReopenApp();

  /// @}


  /// \name Handle application-wide events.
  /// @{

  /// \brief Allow the user to open a file.
  /// At this time, the only files supported are pre-recorded SeeC traces,
  /// which are selected by opening the SeeC Process Trace (.spt) file.
  void OnCommandOpen(wxCommandEvent &Event);

  /// \brief Quit the application.
  void OnCommandExit(wxCommandEvent &Event);

  /// @}


  /// \name TraceViewer specific.
  /// @{

  /// \brief Check if libcurl was initialized successfully.
  ///
  bool checkCURL() const { return CURL; }

  /// \brief Attempt to bring the viewer to the foreground.
  /// If there are traces open, then we will call \c Raise() on each trace
  /// window. Otherwise, we will attempt to show the \c WelcomeFrame.
  ///
  void Raise();

  void HandleFatalError(wxString Description);
  
  /// \brief Notify that a top-level window is being added.
  void addTopLevelWindow(wxWindow *Window) {
    TopLevelWindows.insert(Window);
  }

  /// \brief Notify that the welcome window is being destroyed.
  void removeTopLevelWindow(WelcomeFrame *Window) {
    if (!Welcome)
      return;

    assert(Welcome == Window);
    Welcome = nullptr;
  }

  /// \brief Notify that a top-level window is being destroyed.
  void removeTopLevelWindow(wxWindow *Window) {
    TopLevelWindows.erase(Window);
  }

  /// \brief Get the \c ActionRecordingSubmitter, if there is one.
  ///
  ActionRecordingSubmitter *getActionRecordingSubmitter() const;

  /// @}

private:
  DECLARE_EVENT_TABLE()
};

DECLARE_APP(TraceViewerApp)

#endif // SEEC_TRACE_VIEW_TRACEVIEWERAPP_HPP
