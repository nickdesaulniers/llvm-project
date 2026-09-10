// RUN: %check_clang_tidy %s readability-use-try-operator %t -- -config="{CheckOptions: {readability-use-try-operator.FoldDeclarations: 'true'}}" -- -std=c11

#define NULL ((void *)0)

void *fallible_ptr_op(void);

// Pointer uninitialized declaration folding
void *test_fold_decl(void) {
  void *p;
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: remove unused variable 'p' [readability-use-try-operator]
  // CHECK-FIXES-NOT: void *p;
  p = fallible_ptr_op();
  if (!p)
    return NULL;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to check for errors [readability-use-try-operator]
  // CHECK-FIXES: void *p = fallible_ptr_op()?;
  return p;
}
