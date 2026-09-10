// RUN: %check_clang_tidy %s readability-use-try-operator %t -- -- -std=c11

#define NULL ((void *)0)

struct mutex {};
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);

int fallible_int_op(void);
int fallible_int_op2(int x);
void *fallible_ptr_op(void);

struct State {
  int value;
  struct State *next;
};

struct State *get_state(void);

// 1. Status check where status variable is dead after check
int test_status_check(void) {
  int ret;
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: remove unused variable 'ret' [readability-use-try-operator]
  // CHECK-FIXES-NOT: int ret;
  ret = fallible_int_op();
  if (ret)
    return ret;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to propagate errors [readability-use-try-operator]
  // CHECK-FIXES: fallible_int_op()?;

  ret = fallible_int_op2(42);
  if (ret)
    return ret;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to propagate errors [readability-use-try-operator]
  // CHECK-FIXES: fallible_int_op2(42)?;

  ret = fallible_int_op();
  return ret;
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: use the '?' try operator in return statement [readability-use-try-operator]
  // CHECK-FIXES: return fallible_int_op()?;
}

// 2. Status variable remains live
int test_live_status(void) {
  int ret;
  ret = fallible_int_op();
  if (ret)
    return ret;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to check for errors [readability-use-try-operator]
  // CHECK-FIXES: ret = fallible_int_op()?;
  return ret + 1;
}

// 3. Pointer null check with DeclStmt
void *test_ptr_decl(void) {
  void *p = fallible_ptr_op();
  if (!p)
    return NULL;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to check for errors [readability-use-try-operator]
  // CHECK-FIXES: void *p = fallible_ptr_op()?;
  return p;
}

// 4. Chained member access
struct State *test_member_chain(void) {
  struct State *s;
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: remove unused variable 's' [readability-use-try-operator]
  // CHECK-FIXES-NOT: struct State *s;
  s = get_state();
  if (!s)
    return NULL;
  return s->next;
  // CHECK-MESSAGES: :[[@LINE-4]]:3: warning: use the '?->' try operator to chain member access [readability-use-try-operator]
  // CHECK-FIXES: return get_state()?->next;
}

// 5. Mutex guard and try operator
int test_mutex_guard(struct mutex *m) {
  int ret;
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: remove unused variable 'ret' [readability-use-try-operator]
  // CHECK-FIXES-NOT: int ret;
  mutex_lock(m);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use 'guard(mutex)' and the '?' try operator for resource cleanup [readability-use-try-operator]
  // CHECK-FIXES: guard(mutex)(m);

  ret = fallible_int_op();
  if (ret)
    goto out_unlock;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to propagate errors [readability-use-try-operator]
  // CHECK-FIXES: fallible_int_op()?;

  ret = fallible_int_op2(1);
  if (ret)
    goto out_unlock;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to propagate errors [readability-use-try-operator]
  // CHECK-FIXES: fallible_int_op2(1)?;

out_unlock:
  mutex_unlock(m);
  return ret;
  // CHECK-MESSAGES: :[[@LINE-3]]:1: warning: eliminate cleanup label and return 0 directly [readability-use-try-operator]
  // CHECK-FIXES: return 0;
}

// 6. Pointer uninitialized declaration (default: no folding)
void *test_unfolded_decl(void) {
  void *p;
  // CHECK-FIXES: void *p;
  p = fallible_ptr_op();
  if (!p)
    return NULL;
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: use the '?' try operator to check for errors [readability-use-try-operator]
  // CHECK-FIXES: p = fallible_ptr_op()?;
  return p;
}

// 7. Incompatible error return type should not be converted
int test_incompatible_return(void) {
  void *p = fallible_ptr_op();
  if (!p)
    return -1;
  return 0;
}
