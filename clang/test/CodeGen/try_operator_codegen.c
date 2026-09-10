// RUN: %clang_cc1 -std=c2y -emit-llvm -O0 -Wall -Werror -o - %s | FileCheck %s

int *get_ptr(void);
int get_int(void);

// CHECK-LABEL: define {{.*}}ptr @test_ptr_return()
// CHECK: %[[CALL:.*]] = call ptr @get_ptr()
// CHECK: %[[ISNULL:.*]] = icmp eq ptr %[[CALL]], null
// CHECK: br i1 %[[ISNULL]], label %[[ERRBB:.*]], label %[[CONTBB:.*]]
// CHECK: [[ERRBB]]:
// CHECK: store ptr null, ptr %{{.*}}
// CHECK: br label %[[RETBB:.*]]
// CHECK: [[CONTBB]]:
// CHECK: store ptr %[[CALL]], ptr %{{.*}}
// CHECK: br label %[[RETBB]]
int *test_ptr_return(void) {
  return get_ptr()?;
}

// CHECK-LABEL: define {{.*}}i32 @test_int_return()
// CHECK: %[[CALL:.*]] = call i32 @get_int()
// CHECK: %[[ISERR:.*]] = icmp ne i32 %[[CALL]], 0
// CHECK: br i1 %[[ISERR]], label %[[ERRBB:.*]], label %[[CONTBB:.*]]
// CHECK: [[ERRBB]]:
// CHECK: store i32 %[[CALL]], ptr %{{.*}}
// CHECK: br label %[[RETBB:.*]]
// CHECK: [[CONTBB]]:
// CHECK: store i32 %[[CALL]], ptr %{{.*}}
// CHECK: br label %[[RETBB]]
int test_int_return(void) {
  return get_int()?;
}

// CHECK-LABEL: define {{.*}}i1 @test_bool_return()
// CHECK: %[[CALL:.*]] = call i32 @get_int()
// CHECK: %[[ISERR:.*]] = icmp ne i32 %[[CALL]], 0
// CHECK: br i1 %[[ISERR]], label %[[ERRBB:.*]], label %[[CONTBB:.*]]
// CHECK: [[ERRBB]]:
// CHECK: %[[TOBOOL:.*]] = icmp ne i32 %[[CALL]], 0
// CHECK: store i1 %[[TOBOOL]], ptr %{{.*}}
// CHECK: br label %[[RETBB:.*]]
_Bool test_bool_return(void) {
  return get_int()?;
}

// CHECK-LABEL: define {{.*}}i32 @test_volatile_single_load()
// CHECK: load volatile i32, ptr @v
// CHECK-NOT: load volatile i32, ptr @v
// CHECK: icmp ne i32
volatile int v;
int test_volatile_single_load(void) {
  v?;
  return 0;
}

// CHECK-LABEL: define {{.*}}i32 @test_atomic_single_load()
// CHECK: load atomic i32, ptr @a seq_cst
// CHECK-NOT: load atomic i32, ptr @a seq_cst
// CHECK: icmp ne i32
_Atomic int a;
int test_atomic_single_load(void) {
  a?;
  return 0;
}

// CHECK-LABEL: define {{.*}}i32 @test_cleanup_execution()
// CHECK: call i32 @get_int()
// CHECK: br i1 %{{.*}}, label %[[ERRBB:.*]], label %[[CONTBB:.*]]
// CHECK: [[ERRBB]]:
// CHECK: br label %[[CLEANUPBB:.*]]
// CHECK: [[CLEANUPBB]]:
// CHECK: call void @cleanup_fn(ptr noundef %resource)
// CHECK: ret i32
void cleanup_fn(int *p);
int test_cleanup_execution(void) {
  __attribute__((cleanup(cleanup_fn))) int resource = 42;
  get_int()?;
  return 0;
}
