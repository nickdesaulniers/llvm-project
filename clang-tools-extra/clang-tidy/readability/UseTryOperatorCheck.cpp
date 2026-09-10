//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UseTryOperatorCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

using namespace clang::ast_matchers;
using llvm::DenseMap;
using llvm::DenseSet;
using llvm::SmallVector;

namespace clang::tidy::readability {

namespace {

class FunctionAnalysisVisitor
    : public RecursiveASTVisitor<FunctionAnalysisVisitor> {
public:
  DenseMap<const VarDecl *, SmallVector<const DeclRefExpr *, 4>> Refs;
  DenseSet<const DeclRefExpr *> Writes;
  SmallVector<const CallExpr *, 4> MutexLocks;
  SmallVector<const CallExpr *, 4> MutexUnlocks;
  SmallVector<const GotoStmt *, 4> Gotos;
  DenseMap<const LabelDecl *, const LabelStmt *> Labels;
  DenseMap<const VarDecl *, const DeclStmt *> DeclStmts;

  bool VisitDeclStmt(DeclStmt *DS) {
    for (const Decl *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        DeclStmts[VD] = DS;
      }
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (BO->getOpcode() == BO_Assign) {
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts())) {
        Writes.insert(DRE);
      }
    }
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      Refs[VD].push_back(DRE);
    }
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    if (const auto *Callee = Call->getDirectCallee()) {
      StringRef Name = Callee->getName();
      if (Name == "mutex_lock")
        MutexLocks.push_back(Call);
      else if (Name == "mutex_unlock")
        MutexUnlocks.push_back(Call);
    }
    return true;
  }

  bool VisitGotoStmt(GotoStmt *GS) {
    Gotos.push_back(GS);
    return true;
  }

  bool VisitLabelStmt(LabelStmt *LS) {
    Labels[LS->getDecl()] = LS;
    return true;
  }
};

static bool isAssignmentToVar(const Stmt *S, const VarDecl *&VD,
                             const Expr *&RHS) {
  VD = nullptr;
  RHS = nullptr;
  if (!S)
    return false;
  const auto *E = dyn_cast<Expr>(S);
  if (!E)
    return false;
  E = E->IgnoreImplicit();
  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO || BO->getOpcode() != BO_Assign)
    return false;
  const auto *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
  if (!DRE)
    return false;
  VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return false;
  RHS = BO->getRHS();
  return true;
}

static bool isDeclInitVar(const Stmt *S, const VarDecl *&VD, const Expr *&RHS) {
  VD = nullptr;
  RHS = nullptr;
  if (!S)
    return false;
  const auto *DS = dyn_cast<DeclStmt>(S);
  if (!DS || !DS->isSingleDecl())
    return false;
  VD = dyn_cast<VarDecl>(DS->getSingleDecl());
  if (!VD || !VD->hasInit())
    return false;
  RHS = VD->getInit();
  return true;
}

static bool isIfErrorCheck(const Stmt *S, const VarDecl *VD,
                           const ASTContext &Ctx, const Expr *&RetVal,
                           const GotoStmt *&GS, bool &IsNullCheck,
                           bool &IsNonNullCheck) {
  RetVal = nullptr;
  GS = nullptr;
  IsNullCheck = false;
  IsNonNullCheck = false;
  if (!S)
    return false;
  const auto *If = dyn_cast<IfStmt>(S);
  if (!If || If->getElse() != nullptr)
    return false;

  const Expr *Cond = If->getCond()->IgnoreParenImpCasts();
  bool MatchesVar = false;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    if (DRE->getDecl() == VD) {
      MatchesVar = true;
      IsNonNullCheck = true;
    }
  } else if (const auto *UO = dyn_cast<UnaryOperator>(Cond)) {
    if (UO->getOpcode() == UO_LNot) {
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenImpCasts())) {
        if (DRE->getDecl() == VD) {
          MatchesVar = true;
          IsNullCheck = true;
        }
      }
    }
  } else if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    const auto *DRE_L = dyn_cast<DeclRefExpr>(LHS);
    const auto *DRE_R = dyn_cast<DeclRefExpr>(RHS);
    const DeclRefExpr *DRE = (DRE_L && DRE_L->getDecl() == VD)   ? DRE_L
                             : (DRE_R && DRE_R->getDecl() == VD) ? DRE_R
                                                                 : nullptr;
    const Expr *Other = (DRE == DRE_L) ? RHS : LHS;

    if (DRE) {
      if (BO->getOpcode() == BO_NE) {
        if (Other->isNullPointerConstant(
                const_cast<ASTContext &>(Ctx),
                Expr::NPC_ValueDependentIsNotNull) ||
            Other->isIntegerConstantExpr(const_cast<ASTContext &>(Ctx))) {
          MatchesVar = true;
          IsNonNullCheck = true;
        }
      } else if (BO->getOpcode() == BO_EQ) {
        if (Other->isNullPointerConstant(
                const_cast<ASTContext &>(Ctx),
                Expr::NPC_ValueDependentIsNotNull) ||
            Other->isIntegerConstantExpr(const_cast<ASTContext &>(Ctx))) {
          MatchesVar = true;
          IsNullCheck = true;
        }
      } else if (BO->getOpcode() == BO_LT && DRE == DRE_L) {
        if (auto Val = Other->getIntegerConstantExpr(Ctx)) {
          if (*Val == 0) {
            MatchesVar = true;
            IsNonNullCheck = true;
          }
        }
      }
    }
  }

  if (!MatchesVar)
    return false;

  const Stmt *Then = If->getThen();
  if (const auto *CS = dyn_cast<CompoundStmt>(Then)) {
    if (CS->size() != 1)
      return false;
    Then = CS->body_front();
  }

  if (const auto *RS = dyn_cast<ReturnStmt>(Then)) {
    RetVal = RS->getRetValue();
    if (VD->getType()->isPointerType()) {
      if (!IsNullCheck)
        return false;
      if (!RetVal)
        return false;
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(RetVal->IgnoreParenImpCasts())) {
        if (DRE->getDecl() != VD)
          return false;
      } else if (!RetVal->isNullPointerConstant(
                     const_cast<ASTContext &>(Ctx),
                     Expr::NPC_ValueDependentIsNotNull)) {
        return false;
      }
      return true;
    }

    if (VD->getType()->isIntegerType()) {
      if (!IsNonNullCheck)
        return false;
      if (!RetVal)
        return false;
      const auto *DRE = dyn_cast<DeclRefExpr>(RetVal->IgnoreParenImpCasts());
      if (!DRE || DRE->getDecl() != VD)
        return false;
      return true;
    }

    return false;
  }

  if (const auto *G = dyn_cast<GotoStmt>(Then)) {
    GS = G;
    return true;
  }

  return false;
}

static bool isVarDeadAfter(const VarDecl *VD, SourceLocation Loc,
                           const FunctionAnalysisVisitor &Analysis,
                           const SourceManager &SM) {
  auto It = Analysis.Refs.find(VD);
  if (It == Analysis.Refs.end())
    return true;

  const auto &RefList = It->second;
  for (const DeclRefExpr *DRE : RefList) {
    if (SM.isBeforeInTranslationUnit(Loc, DRE->getLocation())) {
      if (Analysis.Writes.contains(DRE))
        return true;
      return false;
    }
  }
  return true;
}

static CharSourceRange
getStatementRangeWithBlankLine(SourceLocation BeginLoc, SourceLocation EndLoc,
                              bool ConsumeTrailingBlankLine,
                              const SourceManager &SM,
                              const LangOptions &LangOpts) {
  SourceLocation AfterSemi = Lexer::findLocationAfterToken(
      EndLoc, tok::semi, SM, LangOpts, /*SkipTrailingWhitespaceAndNewLine=*/false);
  if (AfterSemi.isInvalid())
    return CharSourceRange::getTokenRange(BeginLoc, EndLoc);

  if (!ConsumeTrailingBlankLine)
    return CharSourceRange::getCharRange(BeginLoc, AfterSemi);

  FileID FID = SM.getFileID(AfterSemi);
  unsigned Offset = SM.getFileOffset(AfterSemi);
  StringRef Buffer = SM.getBufferData(FID);

  size_t Cur = Offset;
  while (Cur < Buffer.size() && (Buffer[Cur] == ' ' || Buffer[Cur] == '\t'))
    Cur++;

  if (Cur < Buffer.size() && Buffer[Cur] == '\r')
    Cur++;
  if (Cur >= Buffer.size() || Buffer[Cur] != '\n')
    return CharSourceRange::getCharRange(BeginLoc, AfterSemi);

  size_t FirstNewline = Cur;
  Cur++;

  while (Cur < Buffer.size() && (Buffer[Cur] == ' ' || Buffer[Cur] == '\t'))
    Cur++;

  if (Cur < Buffer.size() && Buffer[Cur] == '\r')
    Cur++;

  if (Cur < Buffer.size() && Buffer[Cur] == '\n') {
    size_t AfterBlank = Cur + 1;
    while (AfterBlank < Buffer.size() &&
           (Buffer[AfterBlank] == ' ' || Buffer[AfterBlank] == '\t'))
      AfterBlank++;
    if (AfterBlank + 1 < Buffer.size() && Buffer[AfterBlank] == '/' &&
        (Buffer[AfterBlank + 1] == '*' || Buffer[AfterBlank + 1] == '/')) {
      return CharSourceRange::getCharRange(BeginLoc, AfterSemi);
    }
    SourceLocation ExtendedEnd =
        SM.getLocForStartOfFile(FID).getLocWithOffset(FirstNewline + 1);
    return CharSourceRange::getCharRange(BeginLoc, ExtendedEnd);
  }

  return CharSourceRange::getCharRange(BeginLoc, AfterSemi);
}

static std::optional<CharSourceRange>
getDeclStmtRemovalRange(const DeclStmt *DS, const SourceManager &SM,
                        const LangOptions &LangOpts) {
  SourceLocation BeginLoc = DS->getBeginLoc();
  SourceLocation EndLoc = DS->getEndLoc();

  FileID FID = SM.getFileID(BeginLoc);
  if (FID != SM.getFileID(EndLoc))
    return std::nullopt;

  unsigned BeginOffset = SM.getFileOffset(BeginLoc);
  StringRef Buffer = SM.getBufferData(FID);

  size_t LineStart = BeginOffset;
  while (LineStart > 0 && Buffer[LineStart - 1] != '\n' &&
         Buffer[LineStart - 1] != '\r')
    --LineStart;

  bool OnlyWhitespaceBefore = true;
  for (size_t i = LineStart; i < BeginOffset; ++i) {
    if (Buffer[i] != ' ' && Buffer[i] != '\t') {
      OnlyWhitespaceBefore = false;
      break;
    }
  }

  SourceLocation AfterSemi = Lexer::findLocationAfterToken(
      EndLoc, tok::semi, SM, LangOpts, /*SkipTrailingWhitespaceAndNewLine=*/false);
  if (AfterSemi.isInvalid())
    AfterSemi = Lexer::getLocForEndOfToken(EndLoc, 0, SM, LangOpts);

  if (AfterSemi.isInvalid())
    return std::nullopt;

  unsigned EndOffset = SM.getFileOffset(AfterSemi);
  size_t Cur = EndOffset;
  while (Cur < Buffer.size() && (Buffer[Cur] == ' ' || Buffer[Cur] == '\t'))
    Cur++;

  if (Cur < Buffer.size() && Buffer[Cur] == '\r')
    Cur++;
  if (Cur < Buffer.size() && Buffer[Cur] == '\n')
    Cur++;

  if (OnlyWhitespaceBefore) {
    SourceLocation RangeStart =
        SM.getLocForStartOfFile(FID).getLocWithOffset(LineStart);
    SourceLocation RangeEnd = SM.getLocForStartOfFile(FID).getLocWithOffset(Cur);
    return CharSourceRange::getCharRange(RangeStart, RangeEnd);
  }

  return CharSourceRange::getTokenRange(BeginLoc, EndLoc);
}

} // namespace

UseTryOperatorCheck::UseTryOperatorCheck(StringRef Name,
                                         ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      FoldDeclarations(Options.get("FoldDeclarations", false)),
      HandleGuard(Options.get("HandleGuard", true)) {}

void UseTryOperatorCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "FoldDeclarations", FoldDeclarations);
  Options.store(Opts, "HandleGuard", HandleGuard);
}

void UseTryOperatorCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      functionDecl(isDefinition(), unless(isExpansionInSystemHeader()))
          .bind("func"),
      this);
}

void UseTryOperatorCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("func");
  if (!FD || !FD->hasBody())
    return;

  SourceManager &SM = *Result.SourceManager;
  const LangOptions &LangOpts = Result.Context->getLangOpts();
  const ASTContext &Ctx = *Result.Context;

  if (!SM.isWrittenInMainFile(FD->getLocation()))
    return;

  const auto *Body = dyn_cast<CompoundStmt>(FD->getBody());
  if (!Body)
    return;

  FunctionAnalysisVisitor Analysis;
  Analysis.TraverseDecl(const_cast<FunctionDecl *>(FD));

  QualType RetType = FD->getReturnType();
  DenseMap<const VarDecl *, unsigned> EliminatedRefs;
  DenseSet<const VarDecl *> DeclsToRemove;
  DenseSet<const Stmt *> HandledStmts;

  // Pattern 5: Mutex lock + goto error cleanup -> guard(mutex) + '?'
  if (HandleGuard && !Analysis.MutexLocks.empty()) {
    for (const CallExpr *LockCall : Analysis.MutexLocks) {
      if (LockCall->getNumArgs() < 1)
        continue;
      const Expr *LockArg = LockCall->getArg(0);
      StringRef LockArgText = Lexer::getSourceText(
          CharSourceRange::getTokenRange(LockArg->getSourceRange()), SM,
          LangOpts);
      if (LockArgText.empty())
        continue;

      // Find matching mutex_unlock under a LabelStmt
      for (const auto &LabelPair : Analysis.Labels) {
        const LabelStmt *LS = LabelPair.second;
        const auto *UnlockCall = dyn_cast<CallExpr>(LS->getSubStmt());
        if (!UnlockCall || UnlockCall->getNumArgs() < 1)
          continue;
        if (const auto *Callee = UnlockCall->getDirectCallee()) {
          if (Callee->getName() != "mutex_unlock")
            continue;
        } else {
          continue;
        }

        StringRef UnlockArgText = Lexer::getSourceText(
            CharSourceRange::getTokenRange(
                UnlockCall->getArg(0)->getSourceRange()),
            SM, LangOpts);
        if (UnlockArgText != LockArgText)
          continue;

        // Find the statement index of LS in Body
        ArrayRef<Stmt *> Stmts = Body->body();
        size_t LabelIdx = ~0ULL;
        for (size_t k = 0; k < Stmts.size(); ++k) {
          if (Stmts[k] == LS) {
            LabelIdx = k;
            break;
          }
        }
        if (LabelIdx == ~0ULL || LabelIdx + 1 >= Stmts.size())
          continue;

        // Check if next statement is return ret;
        const auto *RS = dyn_cast<ReturnStmt>(Stmts[LabelIdx + 1]);
        if (!RS || !RS->getRetValue())
          continue;
        const auto *RetDRE =
            dyn_cast<DeclRefExpr>(RS->getRetValue()->IgnoreParenImpCasts());
        if (!RetDRE)
          continue;
        const auto *StatusVar = dyn_cast<VarDecl>(RetDRE->getDecl());
        if (!StatusVar)
          continue;

        // Check all gotos to this label
        const LabelDecl *LD = LS->getDecl();
        SmallVector<const GotoStmt *, 4> TargetGotos;
        for (const GotoStmt *GS : Analysis.Gotos) {
          if (GS->getLabel() == LD)
            TargetGotos.push_back(GS);
        }
        if (TargetGotos.empty())
          continue;

        // Verify each goto is in an if (ret) goto LD; preceded by ret = CALL;
        bool AllGotosMatch = true;
        SmallVector<std::pair<const Stmt *, const IfStmt *>, 4> GotoPairs;
        for (size_t k = 0; k < LabelIdx; ++k) {
          if (const auto *If = dyn_cast<IfStmt>(Stmts[k])) {
            const Stmt *Then = If->getThen();
            if (const auto *CS = dyn_cast<CompoundStmt>(Then)) {
              if (CS->size() == 1)
                Then = CS->body_front();
            }
            if (const auto *GS = dyn_cast<GotoStmt>(Then)) {
              if (GS->getLabel() == LD && k > 0) {
                const VarDecl *AssignVD = nullptr;
                const Expr *RHS = nullptr;
                if (isAssignmentToVar(Stmts[k - 1], AssignVD, RHS) &&
                    AssignVD == StatusVar) {
                  GotoPairs.push_back({Stmts[k - 1], If});
                } else {
                  AllGotosMatch = false;
                  break;
                }
              }
            }
          }
        }

        if (!AllGotosMatch || GotoPairs.size() != TargetGotos.size())
          continue;

        // Emit fixits!
        diag(LockCall->getBeginLoc(),
             "use 'guard(mutex)' and the '?' try operator for resource cleanup")
            << FixItHint::CreateReplacement(
                   CharSourceRange::getTokenRange(LockCall->getSourceRange()),
                   ("guard(mutex)(" + LockArgText + ")").str());

        for (size_t g = 0; g < GotoPairs.size(); ++g) {
          const Stmt *AssignStmt = GotoPairs[g].first;
          const IfStmt *If = GotoPairs[g].second;
          const VarDecl *VD = nullptr;
          const Expr *RHS = nullptr;
          isAssignmentToVar(AssignStmt, VD, RHS);

          StringRef CallText = Lexer::getSourceText(
              CharSourceRange::getTokenRange(RHS->getSourceRange()), SM,
              LangOpts);

          bool NextIsErrorCheck = (g + 1 < GotoPairs.size());
          CharSourceRange RepRange = getStatementRangeWithBlankLine(
              AssignStmt->getBeginLoc(), If->getEndLoc(), NextIsErrorCheck, SM,
              LangOpts);

          diag(AssignStmt->getBeginLoc(),
               "use the '?' try operator to propagate errors")
              << FixItHint::CreateReplacement(RepRange,
                                              (CallText + "?;").str());

          HandledStmts.insert(AssignStmt);
          HandledStmts.insert(If);
          EliminatedRefs[StatusVar] += 2; // assignment write + if condition read
        }

        CharSourceRange CleanupRange = getStatementRangeWithBlankLine(
            LS->getBeginLoc(), RS->getEndLoc(), false, SM, LangOpts);
        diag(LS->getBeginLoc(),
             "eliminate cleanup label and return 0 directly")
            << FixItHint::CreateReplacement(CleanupRange, "\treturn 0;");

        HandledStmts.insert(LS);
        HandledStmts.insert(RS);
        EliminatedRefs[StatusVar] += 1; // return ret read

        if (EliminatedRefs[StatusVar] == Analysis.Refs[StatusVar].size()) {
          DeclsToRemove.insert(StatusVar);
        }
      }
    }
  }

  // Helper lambda to process a CompoundStmt
  auto ProcessCompoundStmt = [&](const CompoundStmt *CS) {
    ArrayRef<Stmt *> Stmts = CS->body();
    size_t N = Stmts.size();

    for (size_t i = 0; i < N; ++i) {
      if (HandledStmts.contains(Stmts[i]))
        continue;

      // Pattern 3: Chained member return (?->)
      // V = CALL; if (!V) return NULL; return V->member;
      if (i + 2 < N && !HandledStmts.contains(Stmts[i + 1]) &&
          !HandledStmts.contains(Stmts[i + 2])) {
        const VarDecl *VD = nullptr;
        const Expr *RHS = nullptr;
        bool IsAssign = isAssignmentToVar(Stmts[i], VD, RHS);
        bool IsDecl = !IsAssign && isDeclInitVar(Stmts[i], VD, RHS);

        if ((IsAssign || IsDecl) && VD->getType()->isPointerType() &&
            RetType->isPointerType()) {
          const Expr *RetVal = nullptr;
          const GotoStmt *GS = nullptr;
          bool IsNullCheck = false, IsNonNullCheck = false;
          if (isIfErrorCheck(Stmts[i + 1], VD, Ctx, RetVal, GS, IsNullCheck,
                             IsNonNullCheck) &&
              IsNullCheck) {
            if (const auto *RS = dyn_cast<ReturnStmt>(Stmts[i + 2])) {
              if (RS->getRetValue()) {
                const auto *ME = dyn_cast<MemberExpr>(
                    RS->getRetValue()->IgnoreParenImpCasts());
                if (ME && ME->isArrow()) {
                  const auto *BaseDRE = dyn_cast<DeclRefExpr>(
                      ME->getBase()->IgnoreParenImpCasts());
                  if (BaseDRE && BaseDRE->getDecl() == VD) {
                    // Check if VD has no other references in function
                    unsigned ExpectedRefs = 3; // assign/decl, if check, return
                    if (Analysis.Refs[VD].size() == ExpectedRefs) {
                      StringRef CallText = Lexer::getSourceText(
                          CharSourceRange::getTokenRange(RHS->getSourceRange()),
                          SM, LangOpts);
                      StringRef MemberName =
                          ME->getMemberDecl()->getName();

                      CharSourceRange Range = getStatementRangeWithBlankLine(
                          Stmts[i]->getBeginLoc(), Stmts[i + 2]->getEndLoc(),
                          false, SM, LangOpts);

                      std::string Replacement =
                          ("return " + CallText + "?->" + MemberName + ";").str();

                      diag(Stmts[i]->getBeginLoc(),
                           "use the '?->' try operator to chain member access")
                          << FixItHint::CreateReplacement(Range, Replacement);

                      HandledStmts.insert(Stmts[i]);
                      HandledStmts.insert(Stmts[i + 1]);
                      HandledStmts.insert(Stmts[i + 2]);
                      EliminatedRefs[VD] += ExpectedRefs;
                      DeclsToRemove.insert(VD);
                      i += 2;
                      continue;
                    }
                  }
                }
              }
            }
          }
        }
      }

      // Pattern 1 & 4: Status check / pointer null check
      // S1: V = CALL; (or TYPE V = CALL;)
      // S2: if (COND) return RET;
      if (i + 1 < N && !HandledStmts.contains(Stmts[i + 1])) {
        const VarDecl *VD = nullptr;
        const Expr *RHS = nullptr;
        bool IsAssign = isAssignmentToVar(Stmts[i], VD, RHS);
        bool IsDecl = !IsAssign && isDeclInitVar(Stmts[i], VD, RHS);

        if (IsAssign || IsDecl) {
          const Expr *RetVal = nullptr;
          const GotoStmt *GS = nullptr;
          bool IsNullCheck = false, IsNonNullCheck = false;
          if (isIfErrorCheck(Stmts[i + 1], VD, Ctx, RetVal, GS, IsNullCheck,
                             IsNonNullCheck) &&
              GS == nullptr) {
            // Check compatible return types
            bool TypesCompatible = false;
            if (VD->getType()->isPointerType() && RetType->isPointerType())
              TypesCompatible = true;
            else if (VD->getType()->isIntegerType() && RetType->isIntegerType())
              TypesCompatible = true;
            else if (VD->getType()->isBooleanType() && RetType->isBooleanType())
              TypesCompatible = true;

            if (TypesCompatible) {
              StringRef CallText = Lexer::getSourceText(
                  CharSourceRange::getTokenRange(RHS->getSourceRange()), SM,
                  LangOpts);

              bool IsDead =
                  isVarDeadAfter(VD, Stmts[i + 1]->getEndLoc(), Analysis, SM);

              // Check if next statement is also a dead error check
              bool NextIsErrorCheck = false;
              if (IsDead && i + 2 < N) {
                const VarDecl *NextVD = nullptr;
                const Expr *NextRHS = nullptr;
                if (isAssignmentToVar(Stmts[i + 2], NextVD, NextRHS) ||
                    isDeclInitVar(Stmts[i + 2], NextVD, NextRHS)) {
                  if (isVarDeadAfter(NextVD, Stmts[i + 2]->getEndLoc(), Analysis, SM))
                    NextIsErrorCheck = true;
                }
              }

              CharSourceRange RepRange = getStatementRangeWithBlankLine(
                  Stmts[i]->getBeginLoc(), Stmts[i + 1]->getEndLoc(),
                  NextIsErrorCheck, SM, LangOpts);

              if (IsDead) {
                // Discarded return check: CALL?;
                std::string Replacement = (CallText + "?;").str();
                diag(Stmts[i]->getBeginLoc(),
                     "use the '?' try operator to propagate errors")
                    << FixItHint::CreateReplacement(RepRange, Replacement);

                unsigned RefsInStmts = 0;
                for (const DeclRefExpr *DRE : Analysis.Refs[VD]) {
                  SourceLocation L = DRE->getLocation();
                  if (!SM.isBeforeInTranslationUnit(L, Stmts[i]->getBeginLoc()) &&
                      !SM.isBeforeInTranslationUnit(Stmts[i + 1]->getEndLoc(), L)) {
                    RefsInStmts++;
                  }
                }
                EliminatedRefs[VD] += RefsInStmts;
              } else {
                // Variable is read later: keep assignment or fold declaration
                std::string Replacement;
                if (IsDecl) {
                  // e.g. void *p = alloc()?;
                  StringRef DeclPrefix = Lexer::getSourceText(
                      CharSourceRange::getCharRange(
                          Stmts[i]->getBeginLoc(), RHS->getBeginLoc()),
                      SM, LangOpts);
                  if (DeclPrefix.empty()) {
                    Replacement =
                        (VD->getNameAsString() + " = " + CallText + "?;").str();
                  } else {
                    Replacement = (DeclPrefix + CallText + "?;").str();
                  }
                } else if (FoldDeclarations && !VD->hasInit() &&
                           Analysis.DeclStmts.contains(VD) &&
                           Analysis.DeclStmts.lookup(VD)->isSingleDecl()) {
                  const DeclStmt *DS = Analysis.DeclStmts.lookup(VD);
                  StringRef TypeText = Lexer::getSourceText(
                      CharSourceRange::getCharRange(DS->getBeginLoc(),
                                                    VD->getLocation()),
                      SM, LangOpts);
                  Replacement = (TypeText + VD->getNameAsString() + " = " +
                                 CallText + "?;")
                                    .str();
                  DeclsToRemove.insert(VD);
                } else {
                  Replacement =
                      (VD->getNameAsString() + " = " + CallText + "?;").str();
                }

                diag(Stmts[i]->getBeginLoc(),
                     "use the '?' try operator to check for errors")
                    << FixItHint::CreateReplacement(RepRange, Replacement);

                EliminatedRefs[VD] += 1; // S2 if check
              }

              HandledStmts.insert(Stmts[i]);
              HandledStmts.insert(Stmts[i + 1]);

              if (!IsDecl && EliminatedRefs[VD] == Analysis.Refs[VD].size()) {
                DeclsToRemove.insert(VD);
              }

              i += 1;
              continue;
            }
          }
        }
      }

      // Pattern 2: Terminal return: V = CALL; return V;
      if (i + 1 < N && i + 1 == N - 1 && !HandledStmts.contains(Stmts[i + 1])) {
        const VarDecl *VD = nullptr;
        const Expr *RHS = nullptr;
        bool IsAssign = isAssignmentToVar(Stmts[i], VD, RHS);
        bool IsDecl = !IsAssign && isDeclInitVar(Stmts[i], VD, RHS);

        if (IsAssign || IsDecl) {
          if (const auto *RS = dyn_cast<ReturnStmt>(Stmts[i + 1])) {
            if (RS->getRetValue()) {
              const auto *DRE = dyn_cast<DeclRefExpr>(
                  RS->getRetValue()->IgnoreParenImpCasts());
              if (DRE && DRE->getDecl() == VD) {
                StringRef CallText = Lexer::getSourceText(
                    CharSourceRange::getTokenRange(RHS->getSourceRange()), SM,
                    LangOpts);

                CharSourceRange Range = getStatementRangeWithBlankLine(
                    Stmts[i]->getBeginLoc(), Stmts[i + 1]->getEndLoc(), false,
                    SM, LangOpts);

                std::string Replacement = ("return " + CallText + "?;").str();

                diag(Stmts[i]->getBeginLoc(),
                     "use the '?' try operator in return statement")
                    << FixItHint::CreateReplacement(Range, Replacement);

                HandledStmts.insert(Stmts[i]);
                HandledStmts.insert(Stmts[i + 1]);
                unsigned RefsInStmts = 0;
                for (const DeclRefExpr *DRE : Analysis.Refs[VD]) {
                  SourceLocation L = DRE->getLocation();
                  if (!SM.isBeforeInTranslationUnit(L, Stmts[i]->getBeginLoc()) &&
                      !SM.isBeforeInTranslationUnit(Stmts[i + 1]->getEndLoc(), L)) {
                    RefsInStmts++;
                  }
                }
                EliminatedRefs[VD] += RefsInStmts;

                if (EliminatedRefs[VD] == Analysis.Refs[VD].size()) {
                  DeclsToRemove.insert(VD);
                }

                i += 1;
                continue;
              }
            }
          }
        }
      }
    }
  };

  // Traverse all CompoundStmts in Body
  class CompoundStmtVisitor
      : public RecursiveASTVisitor<CompoundStmtVisitor> {
  public:
    SmallVector<const CompoundStmt *, 8> Compounds;
    bool VisitCompoundStmt(CompoundStmt *CS) {
      Compounds.push_back(CS);
      return true;
    }
  };

  CompoundStmtVisitor CSVisitor;
  CSVisitor.TraverseStmt(const_cast<CompoundStmt *>(Body));
  for (const CompoundStmt *CS : CSVisitor.Compounds) {
    ProcessCompoundStmt(CS);
  }

  // Remove unused variable declarations
  for (const VarDecl *VD : DeclsToRemove) {
    if (!Analysis.DeclStmts.contains(VD))
      continue;
    const DeclStmt *DS = Analysis.DeclStmts.lookup(VD);
    if (!DS->isSingleDecl())
      continue;

    if (auto RemRange = getDeclStmtRemovalRange(DS, SM, LangOpts)) {
      diag(DS->getBeginLoc(), "remove unused variable %0")
          << VD << FixItHint::CreateRemoval(*RemRange);
    }
  }
}

} // namespace clang::tidy::readability

