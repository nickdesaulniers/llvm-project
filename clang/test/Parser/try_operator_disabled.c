// RUN: %clang_cc1 -std=c23 -fsyntax-only -verify=c23 %s
// RUN: %clang_cc1 -x c++ -fsyntax-only -verify=cxx %s

int test_call(void);

int test_disabled(void) {
  return test_call()?; // c23-error {{expected expression}} \
                       // c23-error {{expected ':'}} \
                       // c23-note {{to match this '?'}} \
                       // c23-error {{expected expression}} \
                       // cxx-error {{expected expression}} \
                       // cxx-error {{expected ':'}} \
                       // cxx-note {{to match this '?'}} \
                       // cxx-error {{expected expression}}
}
