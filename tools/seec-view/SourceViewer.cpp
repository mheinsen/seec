//===- tools/seec-trace-view/SourceViewer.cpp -----------------------------===//
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

#include "seec/Clang/MappedAST.hpp"
#include "seec/Clang/MappedFunctionState.hpp"
#include "seec/Clang/MappedModule.hpp"
#include "seec/Clang/MappedProcessTrace.hpp"
#include "seec/Clang/MappedRuntimeErrorState.hpp"
#include "seec/Clang/MappedStateMovement.hpp"
#include "seec/Clang/MappedThreadState.hpp"
#include "seec/Clang/Search.hpp"
#include "seec/ICU/Format.hpp"
#include "seec/ICU/LineWrapper.hpp"
#include "seec/ICU/Resources.hpp"
#include "seec/RuntimeErrors/UnicodeFormatter.hpp"
#include "seec/Trace/TraceSignalInfo.hpp"
#include "seec/Util/MakeFunction.hpp"
#include "seec/Util/Range.hpp"
#include "seec/Util/ScopeExit.hpp"
#include "seec/wxWidgets/AugmentResources.hpp"
#include "seec/wxWidgets/StringConversion.hpp"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <wx/font.h>
#include <wx/timer.h>
#include <wx/tokenzr.h>
#include <wx/stc/stc.h>

#include "unicode/brkiter.h"

#include "ActionRecord.hpp"
#include "ActionReplay.hpp"
#include "ColourSchemeSettings.hpp"
#include "CommonMenus.hpp"
#include "LocaleSettings.hpp"
#include "NotifyContext.hpp"
#include "OpenTrace.hpp"
#include "ProcessMoveEvent.hpp"
#include "SourceViewer.hpp"
#include "SourceViewerSettings.hpp"
#include "StateAccessToken.hpp"
#include "StmtTooltip.hpp"
#include "TraceViewerApp.hpp"
#include "ValueFormat.hpp"

#include <functional>
#include <list>
#include <map>
#include <memory>


//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

wxRect RectFromRange(wxStyledTextCtrl * const Text,
                     int const Start,
                     int const End)
{
  auto const StartLine = Text->LineFromPosition(Start);
  auto const EndLine   = Text->LineFromPosition(End);

  auto const StartPos = Text->PointFromPosition(Start);
  auto const EndPos   = Text->PointFromPosition(End);

  // Calculate the "minimum x" position.
  auto TopLeftX = StartPos.x;
  for (auto Line = StartLine + 1; Line <= EndLine; ++Line) {
    auto const Pos   = Text->GetLineIndentPosition(Line);
    auto const Point = Text->PointFromPosition(Pos);
    if (Point.x < TopLeftX)
      TopLeftX = Point.x;
  }

  // Calculate the "maximum x" position.
  auto BottomRightX = EndPos.x;
  for (auto Line = StartLine; Line < EndLine; ++Line) {
    auto const Pos   = Text->GetLineEndPosition(Line);
    auto const Point = Text->PointFromPosition(Pos);
    if (Point.x > BottomRightX)
      BottomRightX = Point.x;
  }

  auto const EndHeight = Text->TextHeight(EndLine);

  auto const TopLeft = wxPoint(TopLeftX, StartPos.y);

  auto const BottomRight = wxPoint(BottomRightX, EndPos.y + EndHeight);

  return wxRect(TopLeft, wxSize(BottomRight.x - TopLeft.x,
                                BottomRight.y - TopLeft.y));
}


//------------------------------------------------------------------------------
// SourceFileRange
//------------------------------------------------------------------------------

/// \brief A range in a source file.
///
struct SourceFileRange {
  clang::FileEntry const *File;
  
  unsigned Start;
  
  unsigned StartLine;
  
  unsigned StartColumn;
  
  unsigned End;
  
  unsigned EndLine;
  
  unsigned EndColumn;
  
  SourceFileRange()
  : File(nullptr),
    Start(0),
    StartLine(0),
    StartColumn(0),
    End(0),
    EndLine(0),
    EndColumn(0)
  {}
  
  SourceFileRange(clang::FileEntry const *WithFile,
                  unsigned WithStart,
                  unsigned WithStartLine,
                  unsigned WithStartColumn,
                  unsigned WithEnd,
                  unsigned WithEndLine,
                  unsigned WithEndColumn)
  : File(WithFile),
    Start(WithStart),
    StartLine(WithStartLine),
    StartColumn(WithStartColumn),
    End(WithEnd),
    EndLine(WithEndLine),
    EndColumn(WithEndColumn)
  {}
};

/// \brief Get the range in the outermost file of two locations.
///
static SourceFileRange getRangeOutermost(clang::SourceLocation Start,
                                         clang::SourceLocation End,
                                         clang::ASTContext const &AST)
{
  auto const &SourceManager = AST.getSourceManager();
  
  // Find the first character in the first token.
  if (SourceManager.isMacroArgExpansion(Start))
    Start = SourceManager.getSpellingLoc(Start);
  else if (Start.isMacroID())
    Start = SourceManager.getExpansionLoc(Start);
  
  // Find the file that the first token belongs to.
  auto const FileID = SourceManager.getFileID(Start);
  auto const File = SourceManager.getFileEntryForID(FileID);
  
  // Find the first character in the last token.
  if (SourceManager.isMacroArgExpansion(End))
    End = SourceManager.getSpellingLoc(End);
  else if (End.isMacroID())
    End = SourceManager.getExpansionRange(End).second;
  
  if (SourceManager.getFileID(End) != FileID)
    return SourceFileRange{};
  
  // Find the first character following the last token.
  auto const FollowingEnd =
    clang::Lexer::getLocForEndOfToken(End,
                                      0,
                                      SourceManager,
                                      AST.getLangOpts());
  
  // Get the file offset of the start and end.
  auto const StartOffset = SourceManager.getFileOffset(Start);
  
  auto const EndOffset = FollowingEnd.isValid()
                       ? SourceManager.getFileOffset(FollowingEnd)
                       : SourceManager.getFileOffset(End);
  
  return SourceFileRange(File,
                         StartOffset,
                         SourceManager.getLineNumber(FileID, StartOffset),
                         SourceManager.getColumnNumber(FileID, StartOffset),
                         EndOffset,
                         SourceManager.getLineNumber(FileID, EndOffset),
                         SourceManager.getColumnNumber(FileID, EndOffset));
}

/// \brief Get the range in the outermost file that contains a Stmt.
///
static SourceFileRange getRangeOutermost(::clang::Stmt const *Stmt,
                                         ::clang::ASTContext const &AST)
{
  return getRangeOutermost(Stmt->getLocStart(), Stmt->getLocEnd(), AST);
}

/// \brief Get the range in the outermost file that contains a Decl.
///
static SourceFileRange getRangeOutermost(::clang::Decl const *Decl,
                                         ::clang::ASTContext const &AST)
{
  return getRangeOutermost(Decl->getLocStart(), Decl->getLocEnd(), AST);
}

/// \brief Get the range of two locations in a specific FileEntry.
///
static SourceFileRange getRangeInFile(clang::SourceLocation Start,
                                      clang::SourceLocation End,
                                      clang::ASTContext const &AST,
                                      clang::FileEntry const *FileEntry)
{
  auto const &SrcMgr = AST.getSourceManager();
  
  if (SrcMgr.isMacroArgExpansion(Start)) {
    auto const SpellStart = SrcMgr.getSpellingLoc(Start);
    if (SrcMgr.getFileEntryForID(SrcMgr.getFileID(SpellStart)) == FileEntry) {
      Start = SpellStart;
    }
  }

  // Take the expansion location of the Start until it is in the requested file.
  while (SrcMgr.getFileEntryForID(SrcMgr.getFileID(Start)) != FileEntry) {
    if (!Start.isMacroID())
      return SourceFileRange{};
    
    Start = SrcMgr.getImmediateExpansionRange(Start).first;
  }
  
  auto const FileID = SrcMgr.getFileID(Start);
  
  if (SrcMgr.isMacroArgExpansion(End)) {
    auto const SpellEnd = SrcMgr.getSpellingLoc(End);
    if (SrcMgr.getFileID(SpellEnd) == FileID) {
      End = SpellEnd;
    }
  }

  // Take the expansion location of the End until it is in the requested file.
  while (SrcMgr.getFileID(End) != FileID) {
    if (!End.isMacroID())
      return SourceFileRange{};
    
    End = SrcMgr.getImmediateExpansionRange(End).second;
  }
  
  // Find the first character following the last token.
  auto const FollowingEnd =
    clang::Lexer::getLocForEndOfToken(End, 0, SrcMgr, AST.getLangOpts());
  
  // Get the file offset of the start and end.
  auto const StartOffset = SrcMgr.getFileOffset(Start);
  
  auto const EndOffset = FollowingEnd.isValid()
                       ? SrcMgr.getFileOffset(FollowingEnd)
                       : SrcMgr.getFileOffset(End);
  
  return SourceFileRange(FileEntry,
                         StartOffset,
                         SrcMgr.getLineNumber(FileID, StartOffset),
                         SrcMgr.getColumnNumber(FileID, StartOffset),
                         EndOffset,
                         SrcMgr.getLineNumber(FileID, EndOffset),
                         SrcMgr.getColumnNumber(FileID, EndOffset));
}

/// \brief Get the range of a Decl in a specific FileEntry.
///
static SourceFileRange getRangeInFile(clang::Decl const *Decl,
                                      clang::ASTContext const &AST,
                                      clang::FileEntry const *FileEntry)
{
  return getRangeInFile(Decl->getLocStart(), Decl->getLocEnd(), AST, FileEntry);
}

/// \brief Get the range of a Stmt in a specific FileEntry.
///
static SourceFileRange getRangeInFile(clang::Stmt const *Stmt,
                                      clang::ASTContext const &AST,
                                      clang::FileEntry const *FileEntry)
{
  return getRangeInFile(Stmt->getLocStart(), Stmt->getLocEnd(), AST, FileEntry);
}


//------------------------------------------------------------------------------
// Annotation
//------------------------------------------------------------------------------

/// \brief Control wrapping for annotations.
///
enum class WrapStyle {
  None,
  Wrapped
};

/// \brief Text and settings for an annotation.
///
class Annotation {
  /// The text of this annotation.
  UnicodeString Text;
  
  /// The style to use for this annotation's text.
  SciLexerType Style;
  
  /// The wrapping style for this annotation.
  WrapStyle Wrapping;
  
  /// Indent each line with this many spaces.
  long Indent;
  
public:
  /// \brief Constructor.
  ///
  Annotation(UnicodeString const &WithText,
             SciLexerType const WithStyle,
             WrapStyle const WithWrapping)
  : Text(WithText),
    Style(WithStyle),
    Wrapping(WithWrapping),
    Indent(0)
  {}
  
  /// \brief Accessors.
  /// @{
  
  UnicodeString const &getText() const {
    return Text;
  }
  
  SciLexerType getStyle() const {
    return Style;
  }
  
  WrapStyle getWrapping() const {
    return Wrapping;
  }
  
  long getIndent() const {
    return Indent;
  }
  
  /// @} (Accessors)
  
  /// \brief Mutators.
  /// @{
  
  /// \brief Set the indentation of this annotation.
  ///
  void setIndent(long const Value) {
    Indent = Value;
  }
  
  /// @} (Mutators)
};


//------------------------------------------------------------------------------
// SourceFilePanel
//------------------------------------------------------------------------------

wxDEFINE_EVENT(EVT_SOURCE_ANNOTATION_RERENDER, wxCommandEvent);

/// \brief Viewer for a single source code file.
///
class SourceFilePanel : public wxPanel {
  /// \brief Store information about an indicated region.
  ///
  struct IndicatedRegion {
    int Indicator;
    
    int Start;
    
    int Length;
    
    IndicatedRegion(int TheIndicator,
                    int RegionStart,
                    int RegionLength)
    : Indicator(TheIndicator),
      Start(RegionStart),
      Length(RegionLength)
    {}
  };
  

  /// The \c SourceViewerPanel that contains this \c SourceFilePanel.
  SourceViewerPanel *Parent;

  /// The \c ActionRecord for the \c TraceViewerFrame that owns this panel.
  ActionRecord *Recording;

  /// The ASTUnit that the file belongs to.
  clang::ASTUnit *AST;
  
  /// The file entry.
  clang::FileEntry const *File;
  
  /// Text control that displays the file.
  wxStyledTextCtrl *Text;
  
  /// Used to perform line wrapping.
  std::unique_ptr<BreakIterator> Breaker;
  
  /// Access to the current state.
  std::shared_ptr<StateAccessToken> CurrentAccess;
  
  /// The current process state.
  seec::cm::ProcessState const *CurrentProcess;
  
  /// The current thread state.
  seec::cm::ThreadState const *CurrentThread;
  
  /// Regions that have indicators for the current state.
  std::vector<IndicatedRegion> StateIndications;
  
  /// Annotations for the current state, indexed by line.
  std::multimap<int, Annotation> StateAnnotations;
  
  /// Regions that have temporary indicators (e.g. highlighting).
  std::list<IndicatedRegion> TemporaryIndicators;
  
  /// Caches the current mouse position.
  int CurrentMousePosition;
  
  /// Decl that the mouse is currently hovering over.
  clang::Decl const *HoverDecl;
  
  /// Stmt that the mouse is currently hovering over.
  clang::Stmt const *HoverStmt;
  
  /// Temporary indicator for the node that the mouse is hovering over.
  decltype(TemporaryIndicators)::iterator HoverIndicator;

  /// Temporary indicator for the node that the replay is hovering over.
  decltype(TemporaryIndicators)::iterator ReplayIndicator;
  
  /// Timer to determine how long the mouse has hovered over the current node.
  wxTimer HoverTimer;

  /// Used to determine if the mouse remains stationary during a click.
  bool ClickUnmoved;


  /// \brief Setup the Scintilla styles.
  ///
  void setSTCStyles(ColourScheme const &Scheme) {
    setupStylesFromColourScheme(*Text, Scheme);
  }

  /// \brief Setup the Scintilla preferences.
  ///
  void setSTCPreferences() {
    // Set the lexer to C++.
    Text->SetLexer(wxSTC_LEX_CPP);

    setSTCStyles(*wxGetApp().getColourSchemeSettings().getColourScheme());
    
    //
    UErrorCode Status = U_ZERO_ERROR;
    auto KeywordRes = seec::getResource("TraceViewer",
                                        getLocale(),
                                        Status,
                                        "ScintillaKeywords",
                                        "C");
    if (U_SUCCESS(Status)) {
      // Setup the keywords used by the lexer.
      auto Size = KeywordRes.getSize();
      
      for (int32_t i = 0; i < Size; ++i) {
        auto UniStr = KeywordRes.getStringEx(i, Status);
        if (U_FAILURE(Status))
          break;
        
        Text->SetKeyWords(i, seec::towxString(UniStr));
      }
    }

    // Setup the line number margin (initially invisible).
    Text->SetMarginType(static_cast<int>(SciMargin::LineNumber),
                        wxSTC_MARGIN_NUMBER);
    Text->SetMarginWidth(static_cast<int>(SciMargin::LineNumber), 0);
    
    // Annotations.
    Text->AnnotationSetVisible(wxSTC_ANNOTATION_STANDARD);
    
    // Miscellaneous.
    Text->SetIndentationGuides(true);
    Text->SetEdgeColumn(80);
    Text->SetWrapMode(wxSTC_WRAP_NONE);
    
    Text->SetExtraDescent(2);
  }

  /// \brief Set the margin size based on the length of the current text.
  ///
  void setSTCMarginWidth() {
    // Set the width of the line numbers margin.
    auto LineCount = Text->GetLineCount();

    unsigned Digits = 1;
    while (LineCount /= 10)
      ++Digits;

    auto CharWidth = Text->TextWidth(wxSTC_STYLE_LINENUMBER, wxT("0"));

    auto MarginWidth = (Digits + 1) * CharWidth;

    Text->SetMarginWidth(static_cast<int>(SciMargin::LineNumber), MarginWidth);
  }

  /// \brief Handle file loading.
  ///
  void setFileSpecificOptions() {
    // Don't allow the user to edit the source code, because it will ruin our
    // mapping information.
    Text->SetReadOnly(true);
    
    setSTCMarginWidth();
    
    // Clear the selection.
    Text->Clear();
  }
  
  /// \brief Render annotations for a specific line.
  ///
  /// Wrapped annotations do not currently support indentation. If an annotation
  /// has non-zero indentation and WrapStyle::Wrapped, then the indentation will
  /// be ignored.
  ///
  void renderAnnotationsFor(int const Line) {
    // Calculate the width of the text region, in case we do any wrapping.
    auto const MarginLineNumber = static_cast<int>(SciMargin::LineNumber);
    auto const ClientSize = Text->GetClientSize();
    auto const Width = ClientSize.GetWidth()
                       - Text->GetMarginWidth(MarginLineNumber);
    
    std::string Buffer;
    wxString CompleteString;
    wxString Styles;
    
    auto const StyleDefault = static_cast<char>(wxSTC_STYLE_DEFAULT);
    
    for (auto const &LA : seec::range(StateAnnotations.equal_range(Line))) {
      auto const &Anno = LA.second;
      auto const &AnnoText = Anno.getText();
      auto const Indent = Anno.getIndent();
      auto const Style = static_cast<int>(Anno.getStyle());
      
      wxString Spacing(' ', Indent);
      wxString SpacingStyle(StyleDefault, Indent);
      
      switch (Anno.getWrapping()) {
        case WrapStyle::None:
        {
          if (!CompleteString.IsEmpty()) {
            CompleteString += "\n";
            Styles.Append(StyleDefault, std::strlen("\n"));
          }
          
          auto const Length = AnnoText.length();
          int32_t FragStart = 0;
          
          while (FragStart < Length) {
            auto const NewlineIdx = AnnoText.indexOf('\n', FragStart);
            auto const FragEnd = NewlineIdx != -1 ? NewlineIdx : Length;
            
            // Add the indentation for this line.
            CompleteString += Spacing;
            Styles         += SpacingStyle;
            
            // Add the fragment for this line. The style array needs to match
            // the UTF-8 representation of the string.
            auto const Frag = AnnoText.tempSubStringBetween(FragStart, FragEnd);
            
            Buffer.clear();
            Frag.toUTF8String(Buffer);
            
            CompleteString += wxString(Buffer.c_str(), wxConvUTF8);
            Styles.Append(static_cast<char>(Style), Buffer.size());
            
            FragStart = FragEnd + 1;
          }
          
          break;
        }
        
        case WrapStyle::Wrapped:
        {
          auto const Wrappings =
            seec::wrapParagraph(*Breaker, AnnoText,
              [=] (UnicodeString const &Line) -> bool {
                return Text->TextWidth(Style, seec::towxString(Line)) < Width;
              });
          
          for (auto const &Wrapping : Wrappings) {
            if (!CompleteString.IsEmpty()) {
              CompleteString += "\n";
              Styles.Append(StyleDefault, std::strlen("\n"));
            }
            
            // Add the fragment for this line. The style array needs to match
            // the UTF-8 representation of the string.
            auto const Limit = Wrapping.End - Wrapping.TrailingWhitespace;
            auto const Frag = AnnoText.tempSubStringBetween(Wrapping.Start,
                                                            Limit);
            
            Buffer.clear();
            Frag.toUTF8String(Buffer);
            
            CompleteString += wxString(Buffer.c_str(), wxConvUTF8);
            Styles.Append(static_cast<char>(Style), Buffer.size());
          }
          
          break;
        }
      }
    }
    
    Text->AnnotationSetText(Line, CompleteString);
    Text->AnnotationSetStyles(Line, Styles);
  }
  
  /// \brief Render all annotations.
  ///
  void renderAnnotations() {
    for (auto It = StateAnnotations.begin(), End = StateAnnotations.end();
         It != End;
         It = StateAnnotations.upper_bound(It->first))
    {
      renderAnnotationsFor(It->first);
    }
  }
  
  
  /// \name Mouse events.
  /// @{
  
  /// \brief Clear hover nodes and indicators.
  ///
  void clearHoverNode();
  
  /// \brief Called when the mouse moves in the Text (Scintilla) window.
  ///
  void OnTextMotion(wxMouseEvent &Event);
  
  /// \brief Called when the mouse enters the Text (Scintilla) window.
  void OnTextEnterWindow(wxMouseEvent &Event) {
    Parent->OnMouseEnter(*this);
    Event.Skip();
  }

  /// \brief Called when the mouse leaves the Text (Scintilla) window.
  ///
  void OnTextLeaveWindow(wxMouseEvent &Event) {
    CurrentMousePosition = -1;
    clearHoverNode();
    Parent->OnMouseLeave(*this);
    Event.Skip();
  }
  
  /// \brief Called when the mouse's right button is pressed in the Text window.
  ///
  void OnTextRightDown(wxMouseEvent &Event) {
    ClickUnmoved = true;
  }
  
  /// \brief Called when the mouse's right button is released in the Text
  ///        window.
  ///
  void OnTextRightUp(wxMouseEvent &Event) {
    if (!ClickUnmoved)
      return;
    
    if (HoverDecl) {
      HoverTimer.Stop();
      Parent->OnRightClick(*this, HoverDecl);

      wxMenu CM{};
      addDeclAnnotationEdit(CM, this, *(this->Parent->getTrace()), HoverDecl);
      PopupMenu(&CM);
    }
    
    if (HoverStmt) {
      HoverTimer.Stop();
      Parent->OnRightClick(*this, HoverStmt);

      auto const MaybeIndex = CurrentProcess->getThreadIndex(*CurrentThread);
      if (!MaybeIndex.assigned<std::size_t>())
        return;
      
      auto const ThreadIndex = MaybeIndex.get<std::size_t>();
      
      wxMenu CM{};
      addStmtNavigation(*this,
                        CurrentAccess,
                        CM,
                        ThreadIndex,
                        HoverStmt,
                        Recording);
      CM.AppendSeparator();
      addStmtAnnotationEdit(CM, this, *(this->Parent->getTrace()), HoverStmt);
      PopupMenu(&CM);
    }
  }
  
  /// @} (Mouse events.)
  
  /// \brief Called when the mouse has hovered on a node.
  ///
  void OnHover(wxTimerEvent &Ev)
  {
    if (HoverIndicator == end(TemporaryIndicators))
      return;

    auto const Trace = Parent->getTrace();
    assert(Trace && "SourceViewerPanel has no trace!");

    auto const Start = HoverIndicator->Start;
    auto const End   = Start + HoverIndicator->Length;

    auto const ClientRect = RectFromRange(Text, Start, End);

    wxRect ScreenRect(ClientToScreen(ClientRect.GetTopLeft()),
                      ClientRect.GetSize());

    // Determine a good maximum width for the tip window.
    auto const WindowSize = GetSize();
    auto const TipWidth = WindowSize.GetWidth();

    if (HoverDecl) {
      makeDeclTooltip(this, *Trace, HoverDecl, TipWidth, ScreenRect);
    }
    else if (HoverStmt) {
      makeStmtTooltip(this, *Trace, HoverStmt, TipWidth, ScreenRect);
    }
  }

public:
  /// Type used to reference temporary indicators.
  typedef decltype(TemporaryIndicators)::iterator
          temporary_indicator_token;
  
  /// \brief Construct without creating.
  ///
  SourceFilePanel()
  : wxPanel(),
    Parent(nullptr),
    Recording(nullptr),
    AST(nullptr),
    File(nullptr),
    Text(nullptr),
    Breaker(nullptr),
    CurrentAccess(),
    CurrentProcess(nullptr),
    CurrentThread(nullptr),
    StateIndications(),
    StateAnnotations(),
    TemporaryIndicators(),
    CurrentMousePosition(-1),
    HoverDecl(nullptr),
    HoverStmt(nullptr),
    HoverIndicator(TemporaryIndicators.end()),
    ReplayIndicator(TemporaryIndicators.end()),
    HoverTimer(),
    ClickUnmoved(false)
  {}

  /// \brief Construct and create.
  ///
  SourceFilePanel(SourceViewerPanel *WithParent,
                  ActionRecord &WithRecording,
                  clang::ASTUnit &WithAST,
                  clang::FileEntry const *WithFile,
                  llvm::MemoryBuffer const &Buffer,
                  wxWindowID ID = wxID_ANY,
                  wxPoint const &Position = wxDefaultPosition,
                  wxSize const &Size = wxDefaultSize)
  : SourceFilePanel()
  {
    Create(WithParent,
           WithRecording,
           WithAST,
           WithFile,
           Buffer,
           ID,
           Position,
           Size);
  }

  /// \brief Destructor.
  ///
  virtual ~SourceFilePanel() {}

  /// \brief Create the panel.
  ///
  bool Create(SourceViewerPanel *WithParent,
              ActionRecord &WithRecording,
              clang::ASTUnit &WithAST,
              clang::FileEntry const *WithFile,
              llvm::MemoryBuffer const &Buffer,
              wxWindowID ID = wxID_ANY,
              wxPoint const &Position = wxDefaultPosition,
              wxSize const &Size = wxDefaultSize)
  {
    if (!wxPanel::Create(WithParent, ID, Position, Size))
      return false;

    Parent = WithParent;
    Recording = &WithRecording;
    AST = &WithAST;
    File = WithFile;

    Text = new wxStyledTextCtrl(this, wxID_ANY);
    if (!Text)
      return false;

    // Setup the preferences of Text.
    setSTCPreferences();
    
    // Load the source code into the Scintilla control.
    Text->SetText(wxString::FromUTF8(Buffer.getBufferStart()));
    
    setFileSpecificOptions();

    auto Sizer = new wxBoxSizer(wxHORIZONTAL);
    Sizer->Add(Text, wxSizerFlags().Proportion(1).Expand());
    SetSizerAndFit(Sizer);
    
    // Setup the BreakIterator used for line wrapping.
    UErrorCode Status = U_ZERO_ERROR;
    Breaker.reset(BreakIterator::createLineInstance(getLocale(), Status));
    
    if (U_FAILURE(Status)) {
      Breaker.reset();
      return false;
    }
    
    // When the window is resized, create an event to rerender the annotations.
    // We can't rerender them immediately, because the size of the Text control
    // won't have been updated yet.
    Bind(wxEVT_SIZE, std::function<void (wxSizeEvent &)> {
      [this] (wxSizeEvent &Ev) {
        Ev.Skip();
        
        wxCommandEvent EvRerender {
          EVT_SOURCE_ANNOTATION_RERENDER,
          this->GetId()
        };
        
        EvRerender.SetEventObject(this);
        this->AddPendingEvent(EvRerender);
      }});
    
    // Handle the event to rerender annotations (created by resizing).
    Bind(EVT_SOURCE_ANNOTATION_RERENDER,
      std::function<void (wxCommandEvent &)> {
        [this] (wxCommandEvent &Ev) {
          renderAnnotations();
        }});
    
    // Setup mouse handling.
    Text->Bind(wxEVT_MOTION,       &SourceFilePanel::OnTextMotion,      this);
    Text->Bind(wxEVT_ENTER_WINDOW, &SourceFilePanel::OnTextEnterWindow, this);
    Text->Bind(wxEVT_LEAVE_WINDOW, &SourceFilePanel::OnTextLeaveWindow, this);
    Text->Bind(wxEVT_RIGHT_DOWN,   &SourceFilePanel::OnTextRightDown,   this);
    Text->Bind(wxEVT_RIGHT_UP,     &SourceFilePanel::OnTextRightUp,     this);

    // Detect when the mouse has hovered on a node.
    HoverTimer.Bind(wxEVT_TIMER, &SourceFilePanel::OnHover, this);

    return true;
  }


  /// \brief Update the \c ColourSchemeSettings.
  ///
  void OnColourSchemeSettingsChanged(ColourSchemeSettings const &Settings)
  {
    setSTCStyles(*Settings.getColourScheme());
    setSTCMarginWidth();
  }

  /// \name Accessors.
  /// @{

  /// \brief Get the name of the source file displayed by this panel.
  ///
  llvm::StringRef getFileName() const {
    return File->getName();
  }

  /// @} (Accessors)


  /// \name State display.
  /// @{
  
  /// \brief Clear state-related information.
  ///
  void clearState() {
    // Remove hovers.
    clearHoverNode();

    // Remove temporary indicators.
    for (auto &Region : StateIndications) {
      Text->SetIndicatorCurrent(Region.Indicator);
      Text->IndicatorClearRange(Region.Start, Region.Length);
    }
    
    StateIndications.clear();

    // Remove temporary annotations.
    for (auto const &LineAnno : StateAnnotations) {
      Text->AnnotationClearLine(LineAnno.first);
    }
    
    StateAnnotations.clear();
    
    // wxStyledTextCtrl doesn't automatically redraw after the above.
    Text->Refresh();
    
    CurrentAccess.reset();
    CurrentProcess = nullptr;
    CurrentThread = nullptr;
  }
  
  /// \brief Update this panel to reflect the given state.
  ///
  void show(std::shared_ptr<StateAccessToken> Access,
            seec::cm::ProcessState const &Process,
            seec::cm::ThreadState const &Thread)
  {
    CurrentAccess = std::move(Access);
    CurrentProcess = &Process;
    CurrentThread = &Thread;
  }
  
  /// \brief Set an indicator on a range of text for the current state.
  ///
  bool stateIndicatorAdd(SciIndicatorType Indicator, int Start, int End) {
    auto const IndicatorInt = static_cast<int>(Indicator);
    
    // Set the indicator on the text.
    Text->SetIndicatorCurrent(IndicatorInt);
    Text->IndicatorFillRange(Start, End - Start);
    
    // Save the indicator so that we can clear it in clearState().
    StateIndications.emplace_back(IndicatorInt, Start, End - Start);
    
    return true;
  }

  /// \brief Annotate a line for this state.
  ///
  /// \param Line The line to add the annotation on.
  /// \param Column Indent the annotation to start on this column.
  /// \param AnnotationText The text of the annotation.
  /// \param AnnotationStyle The style to use for the annotation.
  /// \param Wrapping Whether or not the annotation should be wrapped to fit
  ///                 the width of the source panel.
  ///
  void annotateLine(long const Line,
                    long const Column,
                    UnicodeString const &AnnotationText,
                    SciLexerType const AnnotationStyle,
                    WrapStyle const Wrapping)
  {
    auto const CharPosition = Text->PositionFromLine(Line) + Column;
    auto const RealColumn = Text->GetColumn(CharPosition);
    
    auto Anno = Annotation{AnnotationText, AnnotationStyle, Wrapping};
    Anno.setIndent(RealColumn);
    
    auto const IntLine = static_cast<int>(Line);
    
    StateAnnotations.insert(std::make_pair(IntLine, std::move(Anno)));
    
    renderAnnotationsFor(IntLine);
  }
  
  /// @} (State display)
  
  
  /// \name Temporary display.
  /// @{
  
  /// \brief Add a new temporary indicator over the given range.
  ///
  temporary_indicator_token temporaryIndicatorAdd(SciIndicatorType Indicator,
                                                  int Start,
                                                  int End)
  {
    auto const IndicatorInt = static_cast<int>(Indicator);
    
    auto const Token = TemporaryIndicators.emplace(TemporaryIndicators.begin(),
                                                   IndicatorInt,
                                                   Start,
                                                   End - Start);
    
    Text->SetIndicatorCurrent(IndicatorInt);
    Text->IndicatorFillRange(Token->Start, Token->Length);
    Text->Refresh();
    
    return Token;
  }
  
  /// \brief Remove an existing temporary indicator.
  ///
  void temporaryIndicatorRemove(temporary_indicator_token Token)
  {
    if (Token == end(TemporaryIndicators))
      return;

    Text->SetIndicatorCurrent(Token->Indicator);
    Text->IndicatorClearRange(Token->Start, Token->Length);
    
    if (HoverIndicator == Token)
      HoverIndicator = end(TemporaryIndicators);
    if (ReplayIndicator == Token)
      ReplayIndicator = end(TemporaryIndicators);

    TemporaryIndicators.erase(Token);
  }
  
  /// \brief Remove all existing temporary indicators.
  ///
  void temporaryIndicatorRemoveAll()
  {
    while (!TemporaryIndicators.empty()) {
      temporaryIndicatorRemove(TemporaryIndicators.begin());
    }
  }
  
  /// @} (Temporary display)
  
  
  /// \name Display control
  /// @{
  
  /// \brief Scroll enough to make the given range visible.
  ///
  void scrollToRange(SourceFileRange const &Range) {
    if (Range.File != File)
      return;

    // TODO: Newer versions of Scintilla (and thus wxStyledTextCtrl) support
    // scrolling to ensure a range is visible (wxStyledTextCtrl::ScrollRange),
    // so update this function when we upgrade wxWidgets.

    assert(Range.StartLine <= std::numeric_limits<int>::max());
    int const RangeStartSci = Range.StartLine - 1;

    auto const DisplayFirst = Text->GetFirstVisibleLine();
    auto const LinesOnScreen = Text->LinesOnScreen();

    auto const DocFirst = Text->DocLineFromVisible(DisplayFirst);
    auto const DocLast = Text->DocLineFromVisible(DisplayFirst + LinesOnScreen);

    if (DocLast < RangeStartSci)
      Text->ScrollToLine(RangeStartSci);
    else if (DocFirst > RangeStartSci)
      Text->ScrollToLine(RangeStartSci);
  }
  
  /// @} (Display control)

  /// \brief Replay a hover indicator over the given range.
  ///
  void replayHover(SourceFileRange const &Range)
  {
    if (ReplayIndicator != end(TemporaryIndicators))
      temporaryIndicatorRemove(ReplayIndicator);
    ReplayIndicator = temporaryIndicatorAdd(SciIndicatorType::CodeHighlight,
                                            Range.Start, Range.End);
    scrollToRange(Range);
  }
};

void SourceFilePanel::clearHoverNode() {
  HoverDecl = nullptr;
  HoverStmt = nullptr;
  
  if (HoverIndicator != TemporaryIndicators.end()) {
    temporaryIndicatorRemove(HoverIndicator);
    HoverIndicator = TemporaryIndicators.end();
  }

  HoverTimer.Stop();
}

void SourceFilePanel::OnTextMotion(wxMouseEvent &Event) {
  // When we exit this scope, call Event.Skip() so that it is handled by the
  // default handler.
  auto const SkipEvent = seec::scopeExit([&](){ Event.Skip(); });
  
  ClickUnmoved = false;
  
  auto const Pos = Text->CharPositionFromPointClose(Event.GetPosition().x,
                                                    Event.GetPosition().y);
  
  if (Pos == CurrentMousePosition)
    return;
  
  auto const PreviousHoverDecl = HoverDecl;
  auto const PreviousHoverStmt = HoverStmt;

  CurrentMousePosition = Pos;
  clearHoverNode();
  
  if (Pos == wxSTC_INVALID_POSITION)
    return;
  
  auto const MaybeResult = seec::seec_clang::search(*AST, File->getName(), Pos);
  
  if (MaybeResult.assigned<seec::Error>()) {
    wxLogDebug("Search failed!");
    return;
  }
  
  auto const &Result = MaybeResult.get<seec::seec_clang::SearchResult>();
  auto const &ASTContext = AST->getASTContext();
  
  switch (Result.getFoundLast()) {
    case seec::seec_clang::SearchResult::EFoundKind::None:
      break;
    
    case seec::seec_clang::SearchResult::EFoundKind::Decl:
    {
      HoverDecl = Result.getFoundDecl();
      auto const Range = getRangeInFile(HoverDecl, ASTContext, File);
      
      if (Range.File) {
        HoverIndicator = temporaryIndicatorAdd(SciIndicatorType::CodeHighlight,
                                               Range.Start,
                                               Range.End);

        if (PreviousHoverDecl != HoverDecl)
          Parent->OnMouseOver(*this, HoverDecl);
      }

      HoverTimer.Start(1000, wxTIMER_ONE_SHOT);
      
      break;
    }
    
    case seec::seec_clang::SearchResult::EFoundKind::Stmt:
    {
      HoverStmt = Result.getFoundStmt();
      auto const Range = getRangeInFile(HoverStmt, ASTContext, File);
      
      if (Range.File) {
        HoverIndicator = temporaryIndicatorAdd(SciIndicatorType::CodeHighlight,
                                               Range.Start,
                                               Range.End);

        if (PreviousHoverStmt != HoverStmt)
          Parent->OnMouseOver(*this, HoverStmt);
      }

      HoverTimer.Start(1000, wxTIMER_ONE_SHOT);
      
      break;
    }
  }
}


//------------------------------------------------------------------------------
// SourceViewerPanel
//------------------------------------------------------------------------------

/// Workaround because wxAuiNotebook has a bug that breaks member FindPage.
///
static int FindPage(wxBookCtrlBase const *Book, wxWindow const *Page)
{
  for (std::size_t i = 0, Count = Book->GetPageCount(); i < Count; ++i)
    if (Book->GetPage(i) == Page)
      return static_cast<int>(i);

  return wxNOT_FOUND;
}

void SourceViewerPanel::ReplayPageChanged(std::string &File)
{
  for (auto &Page : Pages) {
    if (Page.first->getName() == File) {
      auto const Index = FindPage(Notebook, Page.second);
      if (Index != wxNOT_FOUND)
        Notebook->SetSelection(Index);
      break;
    }
  }
}

void SourceViewerPanel::ReplayMouseEnter(std::string &File)
{
  for (auto &Page : Pages) {
    if (Page.first->getName() == File) {
      Page.second->temporaryIndicatorRemoveAll();
      break;
    }
  }
}

void SourceViewerPanel::ReplayMouseLeave(std::string &File)
{
  for (auto &Page : Pages) {
    if (Page.first->getName() == File) {
      Page.second->temporaryIndicatorRemoveAll();
      break;
    }
  }
}

void SourceViewerPanel::ReplayMouseOverDecl(clang::Decl const *D)
{
  if (D == nullptr) {
    highlightOff();
    return;
  }

  if (!Trace)
    return;

  auto const MappedAST = Trace->getTrace().getMapping().getASTForDecl(D);
  if (!MappedAST)
    return;

  auto const &ASTUnit = MappedAST->getASTUnit();
  auto const Range = getRangeOutermost(D, ASTUnit.getASTContext());
  if (!Range.File)
    return;

  auto const PageIt = Pages.find(Range.File);
  if (PageIt == Pages.end())
    return;

  PageIt->second->replayHover(Range);
}

void SourceViewerPanel::ReplayMouseOverStmt(clang::Stmt const *S)
{
  if (S == nullptr) {
    highlightOff();
    return;
  }

  if (!Trace)
    return;

  auto const MappedAST = Trace->getTrace().getMapping().getASTForStmt(S);
  if (!MappedAST)
    return;

  auto const &ASTUnit = MappedAST->getASTUnit();
  auto const Range = getRangeOutermost(S, ASTUnit.getASTContext());
  if (!Range.File)
    return;

  auto const PageIt = Pages.find(Range.File);
  if (PageIt == Pages.end())
    return;

  PageIt->second->replayHover(Range);
}

void SourceViewerPanel::OnPageChanged(wxAuiNotebookEvent &Ev)
{
  auto const Selection = Ev.GetSelection();
  if (!Recording || Selection == wxNOT_FOUND)
    return;

  auto const Page = static_cast<SourceFilePanel const *>
                                (Notebook->GetPage(Selection));
  if (!Page)
    return;

  Recording->recordEventL("SourceViewerPanel.PageChanged",
                          make_attribute("page", Selection),
                          make_attribute("file", Page->getFileName().str()));
}

void SourceViewerPanel::OnMouseEnter(SourceFilePanel &Page)
{
  auto const PageIndex = Notebook->GetPageIndex(&Page);
  if (!Recording || PageIndex == wxNOT_FOUND)
    return;

  Recording->recordEventL("SourceViewerPanel.MouseEnter",
                          make_attribute("page", PageIndex),
                          make_attribute("file", Page.getFileName().str()));
}

void SourceViewerPanel::OnMouseLeave(SourceFilePanel &Page)
{
  auto const PageIndex = Notebook->GetPageIndex(&Page);
  if (!Recording || PageIndex == wxNOT_FOUND)
    return;

  Recording->recordEventL("SourceViewerPanel.MouseLeave",
                          make_attribute("page", PageIndex),
                          make_attribute("file", Page.getFileName().str()));
}

void SourceViewerPanel::OnMouseOver(SourceFilePanel &Page,
                                    clang::Decl const *Decl)
{
  Notifier->createNotify<ConEvHighlightDecl>(Decl);

  auto const PageIndex = Notebook->GetPageIndex(&Page);
  if (!Recording || PageIndex == wxNOT_FOUND)
    return;

  Recording->recordEventL("SourceViewerPanel.MouseOverDecl",
                          make_attribute("page", PageIndex),
                          make_attribute("file", Page.getFileName().str()),
                          make_attribute("decl", Decl));
}

void SourceViewerPanel::OnMouseOver(SourceFilePanel &Page,
                                    clang::Stmt const *Stmt)
{
  Notifier->createNotify<ConEvHighlightStmt>(Stmt);

  auto const PageIndex = Notebook->GetPageIndex(&Page);
  if (!Recording || PageIndex == wxNOT_FOUND)
    return;

  Recording->recordEventL("SourceViewerPanel.MouseOverStmt",
                          make_attribute("page", PageIndex),
                          make_attribute("file", Page.getFileName().str()),
                          make_attribute("stmt", Stmt));
}

void SourceViewerPanel::OnRightClick(SourceFilePanel &Page,
                                     clang::Decl const *Decl)
{
  auto const PageIndex = Notebook->GetPageIndex(&Page);
  if (!Recording || PageIndex == wxNOT_FOUND)
    return;

  Recording->recordEventL("SourceViewerPanel.MouseRightClickDecl",
                          make_attribute("page", PageIndex),
                          make_attribute("file", Page.getFileName().str()),
                          make_attribute("decl", Decl));
}

void SourceViewerPanel::OnRightClick(SourceFilePanel &Page,
                                     clang::Stmt const *Stmt)
{
  auto const PageIndex = Notebook->GetPageIndex(&Page);
  if (!Recording || PageIndex == wxNOT_FOUND)
    return;

  Recording->recordEventL("SourceViewerPanel.MouseRightClickStmt",
                          make_attribute("page", PageIndex),
                          make_attribute("file", Page.getFileName().str()),
                          make_attribute("stmt", Stmt));
}

SourceViewerPanel::SourceViewerPanel()
: wxPanel(),
  Notebook(nullptr),
  Trace(nullptr),
  Notifier(nullptr),
  m_ColourSchemeSettingsRegistration(),
  Recording(nullptr),
  Pages(),
  CurrentAccess()
{}

SourceViewerPanel::SourceViewerPanel(wxWindow *Parent,
                                     OpenTrace &TheTrace,
                                     ContextNotifier &WithNotifier,
                                     ActionRecord &WithRecording,
                                     ActionReplayFrame &WithReplay,
                                     wxWindowID ID,
                                     wxPoint const &Position,
                                     wxSize const &Size)
: SourceViewerPanel()
{
  Create(Parent,
         TheTrace,
         WithNotifier,
         WithRecording,
         WithReplay,
         ID,
         Position,
         Size);
}

SourceViewerPanel::~SourceViewerPanel()
{}

bool SourceViewerPanel::Create(wxWindow *Parent,
                               OpenTrace &TheTrace,
                               ContextNotifier &WithNotifier,
                               ActionRecord &WithRecording,
                               ActionReplayFrame &WithReplay,
                               wxWindowID ID,
                               wxPoint const &Position,
                               wxSize const &Size)
{
  if (!wxPanel::Create(Parent, ID, Position, Size))
    return false;

  Trace = &TheTrace;
  
  Notifier = &WithNotifier;

  Recording = &WithRecording;

  Notebook = new wxAuiNotebook(this,
                               wxID_ANY,
                               wxDefaultPosition,
                               wxDefaultSize,
                               wxAUI_NB_TOP
                               | wxAUI_NB_TAB_SPLIT
                               | wxAUI_NB_TAB_MOVE
                               | wxAUI_NB_SCROLL_BUTTONS);
  
  auto TopSizer = new wxBoxSizer(wxVERTICAL);
  TopSizer->Add(Notebook, wxSizerFlags(1).Expand());
  SetSizerAndFit(TopSizer);

  // Handle ColourSchemeSettings changes.
  m_ColourSchemeSettingsRegistration =
    wxGetApp().getColourSchemeSettings().addListener(
      [this] (ColourSchemeSettings const &Settings) {
        OnColourSchemeSettingsChanged(Settings);
      }
    );
  
  // Setup notebook event recording.
  Notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED,
                 &SourceViewerPanel::OnPageChanged, this);

  // Setup highlight event handling.
  Notifier->callbackAdd([this] (ContextEvent const &Ev) -> void {
    switch (Ev.getKind()) {
      case ContextEventKind::HighlightDecl:
        {
          auto const &HighlightEv = llvm::cast<ConEvHighlightDecl>(Ev);
          if (auto const TheDecl = HighlightEv.getDecl())
            this->highlightOn(TheDecl);
          else
            this->highlightOff();
          break;
        }
      case ContextEventKind::HighlightStmt:
        {
          auto const &HighlightEv = llvm::cast<ConEvHighlightStmt>(Ev);
          if (auto const TheStmt = HighlightEv.getStmt())
            this->highlightOn(TheStmt);
          else
            this->highlightOff();
          break;
        }
      default:
        break;
    }
  });

  // Load main source files.
  auto &MappedModule = Trace->getTrace().getMapping();
  
  for (auto const MappedAST : MappedModule.getASTs()) {
    if (MappedAST) {
      auto const &Unit = MappedAST->getASTUnit();
      auto const &SrcMgr = Unit.getSourceManager();
      auto const FileEntry = SrcMgr.getFileEntryForID(SrcMgr.getMainFileID());
      loadAndShowFile(FileEntry, *MappedAST);
    }
  }
  
  // Setup replay of recorded actions.
  WithReplay.RegisterHandler("SourceViewerPanel.PageChanged", {{"file"}},
    seec::make_function(this, &SourceViewerPanel::ReplayPageChanged));

  WithReplay.RegisterHandler("SourceViewerPanel.MouseEnter", {{"file"}},
    seec::make_function(this, &SourceViewerPanel::ReplayMouseEnter));

  WithReplay.RegisterHandler("SourceViewerPanel.MouseLeave", {{"file"}},
    seec::make_function(this, &SourceViewerPanel::ReplayMouseLeave));

  WithReplay.RegisterHandler("SourceViewerPanel.MouseOverDecl", {{"decl"}},
    seec::make_function(this, &SourceViewerPanel::ReplayMouseOverDecl));

  WithReplay.RegisterHandler("SourceViewerPanel.MouseOverStmt", {{"stmt"}},
    seec::make_function(this, &SourceViewerPanel::ReplayMouseOverStmt));

  return true;
}

void SourceViewerPanel::clear() {
  Notebook->DeleteAllPages();
  Pages.clear();
}

void SourceViewerPanel::show(std::shared_ptr<StateAccessToken> Access,
                             seec::cm::ProcessState const &Process,
                             seec::cm::ThreadState const &Thread)
{
  // Clear existing state information from all files.
  for (auto &PagePair : Pages)
    PagePair.second->clearState();
  
  // Replace our old access token.
  CurrentAccess = std::move(Access);
  if (!CurrentAccess)
    return;
  
  // Give access to the new state to the SourceFilePanels, before exiting this
  // function (we may create additional SourceFilePanels before that happens).
  auto const GiveState = seec::scopeExit([&] () {
    for (auto &PagePair : Pages)
      PagePair.second->show(CurrentAccess, Process, Thread);
  });
  
  // Lock the current state while we read from it.
  auto Lock = CurrentAccess->getAccess();
  if (!Lock)
    return;
  
  // Find the active function (if any).
  auto const &CallStack = Thread.getCallStack();
  if (CallStack.empty())
    return;
  
  auto const &Function = CallStack.back().get();
  
  if (auto const ActiveStmt = Function.getActiveStmt()) {
    showActiveStmt(ActiveStmt, Function);
  }
  else if (auto const ActiveDecl = Function.getActiveDecl()) {
    showActiveDecl(ActiveDecl, Function);
  }
  else {
    auto const FunctionDecl = Function.getFunctionDecl();
    if (FunctionDecl) {
      showActiveDecl(FunctionDecl, Function);
    }
  }
  
  // Show all active runtime errors.
  for (auto const &RuntimeError : Function.getRuntimeErrorsActive())
    showRuntimeError(RuntimeError, Function);
}

void
SourceViewerPanel
::OnColourSchemeSettingsChanged(ColourSchemeSettings const &Settings)
{
  for (auto &FileAndPanel : Pages)
    FileAndPanel.second->OnColourSchemeSettingsChanged(Settings);
}

void
SourceViewerPanel::showRuntimeError(seec::cm::RuntimeErrorState const &Error,
                                    seec::cm::FunctionState const &InFunction)
{
  auto const &Augmentations = wxGetApp().getAugmentations();

  // Generate a localised textual description of the error.
  auto MaybeDesc = Error.getDescription(Augmentations.getCallbackFn());
  
  if (MaybeDesc.assigned<seec::Error>()) {
    UErrorCode Status = U_ZERO_ERROR;
    
    auto const Str = MaybeDesc.get<seec::Error>().getMessage(Status,
                                                             getLocale());
    
    if (U_SUCCESS(Status)) {
      wxLogDebug("Error getting runtime error description: %s.",
                 seec::towxString(Str));
    }
    
    return;
  }
  
  seec::runtime_errors::DescriptionPrinterUnicode Printer { MaybeDesc.move<0>(),
                                                            "\n",
                                                            " " };
  
  auto const MappedAST = InFunction.getMappedAST();
  if (!MappedAST)
    return;
  
  auto &ASTUnit = MappedAST->getASTUnit();
  
  // Find the source location of the node that caused the error.
  auto const Decl = Error.getDecl();
  auto const Stmt = Error.getStmt();
  if (!Decl && !Stmt) {
    wxLogDebug("Runtime error with no Decl or Stmt!");
    return;
  }

  auto const Range = Stmt ? getRangeOutermost(Stmt, ASTUnit.getASTContext())
                          : getRangeOutermost(Decl, ASTUnit.getASTContext());
  if (!Range.File) {
    wxLogDebug("Couldn't find file for node.");
    return;
  }

  // Attempt to load the containing file and annotate the error message.
  if (auto const Panel = loadAndShowFile(Range.File, *MappedAST)) {
    Panel->annotateLine(Range.EndLine - 1,
                        /* Column */ 0,
                        Printer.getString(),
                        SciLexerType::SeeCRuntimeError,
                        WrapStyle::Wrapped);
  }
}

void showCaughtSignals(SourceFilePanel *Panel,
                       seec::cm::ThreadState const &Thread,
                       unsigned int SciLine)
{
  auto const &Signals = Thread.getCaughtSignals();
  if (Signals.empty()) {
    return;
  }
  
  auto MaybeFormat = seec::getString("Trace", {"descriptions", "CaughtSignal"});
  if (!MaybeFormat.assigned<UnicodeString>()) {
    llvm::errs() << "couldn't get CaughtSignal message.\n";
    return;
  }
  
  auto &Format = MaybeFormat.get<UnicodeString>();
  
  for (auto &Signal : Signals) {
    UErrorCode ICUStatus = U_ZERO_ERROR;
    
    auto Name = Signal.getName() ? Signal.getName() : "NULL";
    
    auto Formatted = seec::icu::format(Format,
                       seec::icu::FormatArgumentsWithNames()
                         .add("name", Name)
                         .add("value", Signal.getSignal())
                         .add("message", Signal.getMessage()),
                       ICUStatus);

    Panel->annotateLine(SciLine, 0,
                        Formatted,
                        SciLexerType::SeeCRuntimeError,
                        WrapStyle::None);
  }
}

void
SourceViewerPanel::showActiveStmt(::clang::Stmt const *Statement,
                                  ::seec::cm::FunctionState const &InFunction)
{
  auto const MappedAST = InFunction.getMappedAST();
  if (!MappedAST)
    return;
  
  auto &ASTUnit = MappedAST->getASTUnit();
  
  auto const Range = getRangeOutermost(Statement, ASTUnit.getASTContext());
  
  if (!Range.File) {
    wxLogDebug("Couldn't find file for Stmt.");
    return;
  }
  
  auto const Panel = loadAndShowFile(Range.File, *MappedAST);
  if (!Panel) {
    wxLogDebug("Couldn't show source panel for file %s.",
               Range.File->getName().str());
    return;
  }
  
  // Show that the Stmt is active.
  Panel->stateIndicatorAdd(SciIndicatorType::CodeActive,
                           Range.Start,
                           Range.End);
  
  // Scroll to the active Stmt.
  Panel->scrollToRange(Range);
  
  auto const Value = InFunction.getStmtValue(Statement);
  if (Value) {
    auto const &Process = InFunction.getParent().getParent();
    auto const String = getPrettyStringForInline(*Value, Process, Statement);
    
    Panel->annotateLine(Range.EndLine - 1,
                        Range.StartColumn - 1,
                        String,
                        SciLexerType::SeeCRuntimeValue,
                        WrapStyle::None);
  }
  
  showCaughtSignals(Panel, InFunction.getParent(), Range.EndLine - 1);
}

void
SourceViewerPanel::showActiveDecl(::clang::Decl const *Declaration,
                                  ::seec::cm::FunctionState const &InFunction)
{
  auto const MappedAST = InFunction.getMappedAST();
  if (!MappedAST)
    return;
  
  auto &ASTUnit = MappedAST->getASTUnit();
  
  auto const Range = getRangeOutermost(Declaration, ASTUnit.getASTContext());
  
  if (!Range.File) {
    wxLogDebug("Couldn't find file for Decl.");
    return;
  }
  
  auto const Panel = loadAndShowFile(Range.File, *MappedAST);
  if (!Panel) {
    wxLogDebug("Couldn't show source panel for file %s.",
               Range.File->getName().str());
    return;
  }
  
  // Show that the Decl is active.
  Panel->stateIndicatorAdd(SciIndicatorType::CodeActive,
                           Range.Start,
                           Range.End);
  
  // Scroll to the active Decl.
  Panel->scrollToRange(Range);
  
  showCaughtSignals(Panel, InFunction.getParent(), Range.EndLine - 1);
}

SourceFilePanel *
SourceViewerPanel::loadAndShowFile(clang::FileEntry const *File,
                                   seec::seec_clang::MappedAST const &MAST)
{
  auto const It = Pages.find(File);
  if (It != Pages.end()) {
    auto const Index = Notebook->GetPageIndex(It->second);
    Notebook->SetSelection(Index);
    return It->second;
  }
  
  // Create a new panel for this file.
  auto &ASTUnit = MAST.getASTUnit();
  auto &SrcMgr = ASTUnit.getSourceManager();
  
  bool Invalid = false;
  auto const Buffer = SrcMgr.getMemoryBufferForFile(File, &Invalid);
  
  if (Invalid) {
    wxLogDebug("loadAndShowFile %s: MemoryBuffer is invalid!",
               File->getName().str());
    return nullptr;
  }

  auto const SourcePanel = new SourceFilePanel(this,
                                               *Recording,
                                               ASTUnit,
                                               File,
                                               *Buffer);
  Pages.insert(std::make_pair(File, SourcePanel));
  Notebook->AddPage(SourcePanel, File->getName().str());
  
  return SourcePanel;
}

void SourceViewerPanel::highlightOn(::clang::Decl const *Decl) {
  if (!Trace)
    return;
  
  auto const &ProcessTrace = Trace->getTrace();
  auto const &MappedModule = ProcessTrace.getMapping();
  auto const MappedAST = MappedModule.getASTForDecl(Decl);
  if (!MappedAST)
    return;
  
  auto const &ASTUnit = MappedAST->getASTUnit();
  auto const Range = getRangeOutermost(Decl, ASTUnit.getASTContext());
  
  if (!Range.File)
    return;
  
  auto const PageIt = Pages.find(Range.File);
  if (PageIt == Pages.end())
    return;
  
  PageIt->second->temporaryIndicatorAdd(SciIndicatorType::CodeHighlight,
                                        Range.Start,
                                        Range.End);
}

void SourceViewerPanel::highlightOn(::clang::Stmt const *Stmt) {
  if (!Trace)
    return;
  
  auto const &ProcessTrace = Trace->getTrace();
  auto const &MappedModule = ProcessTrace.getMapping();
  auto const MappedAST = MappedModule.getASTForStmt(Stmt);
  if (!MappedAST)
    return;
  
  auto const &ASTUnit = MappedAST->getASTUnit();
  auto const Range = getRangeOutermost(Stmt, ASTUnit.getASTContext());
  
  if (!Range.File)
    return;
  
  auto const PageIt = Pages.find(Range.File);
  if (PageIt == Pages.end())
    return;
  
  PageIt->second->temporaryIndicatorAdd(SciIndicatorType::CodeHighlight,
                                        Range.Start,
                                        Range.End);
}

void SourceViewerPanel::highlightOff() {
  for (auto &Page : Pages)
    Page.second->temporaryIndicatorRemoveAll();
}
