// RUN: %clang_cc1 -std=c2y -fsyntax-only -Wall -verify %s

typedef unsigned long size_t;
size_t strlen(const char *);
int printf(const char *, ...);
long strtol(const char *, char **, int);
char *getenv(const char *);

int get_int(void);
int *get_ptr(void);

// Constraint 1: outside a function body
int g_val = get_int()?; // expected-error {{try operator '?' may only appear within a function body}}

// Constraint 2: operand must be identifier or function call
int test_constraint2_operand(int x, int y) {
  1?; // expected-error {{operand of '?' operator must be an identifier designating an object or a function call}}
  (x + y)?; // expected-error {{operand of '?' operator must be an identifier designating an object or a function call}}
  x++; // fine
  (x++)?; // expected-error {{operand of '?' operator must be an identifier designating an object or a function call}}
  return 0;
}

// Constraint 2: operand type must be integer or pointer
struct S { int a; };
int test_constraint2_type(float f, struct S s) {
  f?; // expected-error {{operand of '?' operator must have pointer or integer type (found 'float')}}
  s?; // expected-error {{operand of '?' operator must have pointer or integer type (found 'struct S')}}
  return 0;
}

// Constraint 2: callee containing '?'
typedef int *(*fn_ptr_t)(void);
fn_ptr_t get_fn_ptr(void);
int *test_constraint2_callee(void) {
  get_fn_ptr()?()?; // expected-error {{function designator of a call operand to '?' cannot contain '?'}} \
                    // expected-error {{'?' operator is not allowed in this position}}
  return 0;
}

// Constraint 3: enclosing function must not return void
void test_constraint3_void(void) {
  get_int()?; // expected-error {{'?' operator cannot appear in a function returning 'void'}}
}

// Constraint 3: kind mismatch
int test_constraint3_mismatch_int(int *p) {
  p?; // expected-error {{operand of '?' has pointer type ('int *') but enclosing function 'test_constraint3_mismatch_int' returns integer type ('int')}}
  return 0;
}

int *test_constraint3_mismatch_ptr(int x) {
  x?; // expected-error {{operand of '?' has integer type ('int') but enclosing function 'test_constraint3_mismatch_ptr' returns pointer type ('int *')}}
  return 0;
}

struct S test_constraint3_mismatch_struct(int x) {
  x?; // expected-error {{operand of '?' has integer type ('int') but enclosing function 'test_constraint3_mismatch_struct' returns other type ('struct S')}}
  return (struct S){0};
}

// Enums and qualifiers are allowed
enum Color { RED, GREEN, BLUE };
enum Color get_color(void);
int test_enum_and_qualifiers(volatile int v, const int *p) {
  enum Color c = get_color()?;
  v?;
  return c;
}

const int *test_ptr_qualifier_conversion(int *p) {
  p?;
  return p;
}

// Section 4.2 Library Denylist
int test_denylist_never_fails(const char *s) {
  size_t len = strlen(s)?; // expected-error {{'strlen' never fails; '?' operator cannot be used}} \
                           // expected-note {{remove the '?' operator; 'strlen' always succeeds}}
  return len;
}

int test_denylist_returns_data(void) {
  int n = printf("hello\n")?; // expected-error {{'printf' returns a count or data value rather than a status code; '?' operator cannot be used}}
  return n;
}

int test_denylist_errno(void) {
  long val = strtol("123", 0, 10)?; // expected-error {{'strtol' reports errors through errno, not its return value; '?' operator cannot be used}}
  return val;
}

char *test_denylist_null_miss(void) {
  char *path = getenv("PATH")?; // expected-error {{'getenv' returns a null pointer on a normal search miss; '?' operator cannot be used}}
  return path;
}

int test_unused_value(void) {
  get_int()?; // No unused-value warning!
  42; // expected-warning {{expression result unused}}
  return 0;
}
