// RUN: %clang_cc1 -std=c2y -fsyntax-only -Wunused-value -verify %s

int test_call(void);
int *test_ptr_call(void);

struct S {
  int x;
  int *p;
};
struct S *test_struct_call(void);
typedef int (*fn_t)(void);
fn_t test_fn_call(void);

// Position 1: return statement
int test_pos_return(void) {
  return test_call()?;
}

// Position 2: expression statement
int test_pos_expr_stmt(void) {
  test_call()?;
  return 0;
}

// Position 3: initializer of local variable
int test_pos_local_init(void) {
  int x = test_call()?;
  return x;
}

// Position 4: rhs of simple assignment
int test_pos_simple_assign(void) {
  int x;
  x = test_call()?;
  return x;
}

// Position 4: rhs of assignment to parameter
int test_pos_param_assign(int rc) {
  rc = test_call()?;
  return rc;
}

// Disallowed: assignment to non-identifier LHS
int test_disallowed_non_id_assign(struct S *p, int *arr) {
  p->x = test_call()?; // expected-error {{left operand of assignment with '?' operator must be an identifier designating an object}}
  arr[0] = test_call()?; // expected-error {{left operand of assignment with '?' operator must be an identifier designating an object}}
  return 0;
}

// Postfix chaining: ->, [], ()
// For pointer operands, the enclosing function must return a pointer.
int *test_chaining(int *arr) {
  int *a = test_struct_call()?->p;
  int b = test_ptr_call()?[0];
  int c = test_fn_call()?();
  return a;
}

// Ternary disambiguation: f()?(a) vs f() ? (a) : b
typedef int (*fn_arg_t)(int);
extern fn_arg_t get_fn(void);
fn_arg_t test_ternary_disambiguation(int a, int b) {
  // Postfix ? followed by parenthesized argument in chained call:
  // get_fn()?(a) has no trailing colon, so it's a try operator on get_fn(), then calling (a).
  int res1 = get_fn()?(a);

  // Ternary operator:
  int res2 = test_call() ? (a) : b;
  int res3 = test_call() ? a : b;

  return 0;
}

// Enclosing ternary disambiguation: outer ternary colon must not be consumed by inner try lookahead
fn_arg_t test_enclosing_ternary_disambiguation(int cond, int a, int b) {
  int res1 = cond ? get_fn()?(a) : b; // expected-error {{'?' operator is not allowed in this position}}
  int res2 = cond ? (test_call() ? a : b) : b;
  int res3 = cond ? test_call() ? a : b : b;
  return 0;
}

// Binary operator disambiguation: f()? + 1 parsed as try operator + binop, not malformed ternary
int test_binop_disambiguation(void) {
  int res1 = test_call()? + 1; // expected-error {{'?' operator is not allowed in this position}}
  int res2 = test_call() ? + 1 : 2;
  return 0;
}

// Disallowed in conditions
int test_disallowed_conditions(void) {
  if (test_call()?) {} // expected-error {{'?' operator is not allowed in this position}}
  while (test_call()?) {} // expected-error {{'?' operator is not allowed in this position}}
  do {} while (test_call()?); // expected-error {{'?' operator is not allowed in this position}}
  switch (test_call()?) { case 0: break; } // expected-error {{'?' operator is not allowed in this position}}
  return 0;
}

// Disallowed in for loops
int test_disallowed_for(void) {
  for (test_call()?; ; ) {} // expected-error {{'?' operator is not allowed in this position}}
  for (; test_call()?; ) {} // expected-error {{'?' operator is not allowed in this position}}
  for (; ; test_call()?) {} // expected-error {{'?' operator is not allowed in this position}}
  return 0;
}

// Disallowed in sizeof, _Alignof, typeof
int test_disallowed_unevaluated(void) {
  int s = sizeof(test_call()?); // expected-error {{'?' operator is not allowed in this position}} \
                                // expected-warning {{expression with side effects has no effect in an unevaluated context}}
  __typeof__(test_call()?) t; // expected-error {{'?' operator is not allowed in this position}}
  return 0;
}

// Disallowed in array bound
int test_disallowed_array_bound(void) {
  int arr[test_call()?]; // expected-error {{'?' operator is not allowed in this position}}
  return 0;
}

// Disallowed in compound assignment
int test_disallowed_compound_assign(void) {
  int x = 0;
  x += test_call()?; // expected-error {{'?' operator is not allowed in this position}}
  return x;
}

// Disallowed in nested expressions / arguments
void take_arg(int x);
int test_disallowed_nested(void) {
  take_arg(test_call()?); // expected-error {{'?' operator is not allowed in this position}}
  int y = 1 + test_call()?; // expected-error {{'?' operator is not allowed in this position}}
  return 0;
}

// Disallowed multiple ? in a chain
int *test_disallowed_multi_chain(void) {
  extern struct S *get_s(void);
  int x = get_s()?->p?[0]; // expected-error {{'?' operator is not allowed in this position}} // expected-error {{operand of '?' operator must be an identifier designating an object or a function call}}
  return 0;
}
