//===- tools/seec-trace-view/StmtTooltip.cpp ------------------------------===//
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
#include "seec/Clang/MappedProcessState.hpp"
#include "seec/Clang/MappedRuntimeErrorState.hpp"
#include "seec/Clang/MappedThreadState.hpp"
#include "seec/Clang/MappedValue.hpp"
#include "seec/ClangEPV/ClangEPV.hpp"
#include "seec/wxWidgets/AugmentResources.hpp"
#include "seec/wxWidgets/StringConversion.hpp"

#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"

#include <wx/tipwin.h>

#include "RuntimeValueLookup.hpp"
#include "StmtTooltip.hpp"
#include "TraceViewerApp.hpp"
#include "ValueFormat.hpp"

wxTipWindow *makeDeclTooltip(wxWindow *Parent,
                             clang::Decl const * const Decl,
                             wxCoord MaxLength,
                             wxRect &RectBound)
{
  wxString TipString;

  auto const &Augmentations = wxGetApp().getAugmentations();
  auto Augmenter = Augmentations.getCallbackFn();

  auto const MaybeExplanation = seec::clang_epv::explain(Decl, Augmenter);
  if (MaybeExplanation.assigned(0)) {
    auto const &Explanation = MaybeExplanation.get<0>();
    TipString << seec::towxString(Explanation->getString()) << "\n";
  }

  TipString.Trim();

  if (TipString.size())
    return new wxTipWindow(Parent, TipString, MaxLength, nullptr, &RectBound);

  return nullptr;
}

wxTipWindow *makeStmtTooltip(wxWindow *Parent,
                             clang::Stmt const * const Stmt,
                             wxCoord MaxLength,
                             seec::cm::FunctionState const *ActiveFunction,
                             wxRect *RectBound)
{
  wxString TipString;

  if (ActiveFunction) {
    if (auto const V = ActiveFunction->getStmtValue(Stmt)) {
      auto const &Process = ActiveFunction->getParent().getParent();
      TipString << seec::towxString(getPrettyStringForInline(*V, Process, Stmt))
                << "\n";
    }
  }

  // Add the type of the value.
  if (auto const E = llvm::dyn_cast<clang::Expr>(Stmt))
    TipString << E->getType().getAsString() << "\n";

  auto const &Augmentations = wxGetApp().getAugmentations();
  auto Augmenter = Augmentations.getCallbackFn();

  // Attempt to get a general explanation of the statement.
  auto const MaybeExplanation =
    seec::clang_epv::explain(Stmt,
                             RuntimeValueLookupForFunction(ActiveFunction),
                             Augmenter);

  if (MaybeExplanation.assigned(0)) {
    auto const &Explanation = MaybeExplanation.get<0>();
    if (TipString.size())
      TipString << "\n";
    TipString << seec::towxString(Explanation->getString()) << "\n";
  }

  if (ActiveFunction) {
    // Get any runtime errors related to the Stmt.
    for (auto const &RuntimeError : ActiveFunction->getRuntimeErrors()) {
      if (RuntimeError.getStmt() != Stmt)
        continue;

      auto const MaybeDescription = RuntimeError.getDescription(Augmenter);
      if (MaybeDescription.assigned(0)) {
        auto const &Description = MaybeDescription.get<0>();
        if (TipString.size())
          TipString << "\n";
        TipString << seec::towxString(Description->getString());
      }
    }
  }

  TipString.Trim();

  // Display the generated tooltip (if any).
  if (TipString.size())
    return new wxTipWindow(Parent, TipString, MaxLength, nullptr, RectBound);

  return nullptr;
}

wxTipWindow *makeStmtTooltip(wxWindow *Parent,
                             clang::Stmt const * const Stmt,
                             seec::cm::FunctionState const &ActiveFunction,
                             wxCoord MaxLength,
                             wxRect &RectBound)
{
  return makeStmtTooltip(Parent, Stmt, MaxLength, &ActiveFunction, &RectBound);
}

wxTipWindow *makeStmtTooltip(wxWindow *Parent,
                             clang::Stmt const * const Stmt,
                             wxCoord MaxLength,
                             wxRect &RectBound)
{
  return makeStmtTooltip(Parent, Stmt, MaxLength, nullptr, &RectBound);
}
