//===-- Implementation of longjmp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/setjmp/longjmp.h"
#include "include/llvm-libc-macros/offsetof-macro.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#if !defined(LIBC_TARGET_ARCH_IS_X86)
#error "Invalid file include"
#endif

namespace LIBC_NAMESPACE_DECL {

#ifdef __i386__
[[gnu::naked]]
LLVM_LIBC_FUNCTION(void, longjmp, (jmp_buf, int)) {
  asm(R"(
      mov 0x4(%%esp), %%ecx
      mov 0x8(%%esp), %%eax
      cmpl $0x1, %%eax
      adcl $0x0, %%eax

      mov %c[ebx](%%ecx), %%ebx
      mov %c[esi](%%ecx), %%esi
      mov %c[edi](%%ecx), %%edi
      mov %c[ebp](%%ecx), %%ebp
      mov %c[esp](%%ecx), %%esp

      jmp *%c[eip](%%ecx)
      )" ::[ebx] "i"(offsetof(__jmp_buf, ebx)),
      [esi] "i"(offsetof(__jmp_buf, esi)), [edi] "i"(offsetof(__jmp_buf, edi)),
      [ebp] "i"(offsetof(__jmp_buf, ebp)), [esp] "i"(offsetof(__jmp_buf, esp)),
      [eip] "i"(offsetof(__jmp_buf, eip)));
}
#else
LLVM_LIBC_FUNCTION(void, longjmp, (jmp_buf buf, int val)) {
  register __UINT64_TYPE__ rbx __asm__("rbx");
  register __UINT64_TYPE__ rbp __asm__("rbp");
  register __UINT64_TYPE__ r12 __asm__("r12");
  register __UINT64_TYPE__ r13 __asm__("r13");
  register __UINT64_TYPE__ r14 __asm__("r14");
  register __UINT64_TYPE__ r15 __asm__("r15");
  register __UINT64_TYPE__ rsp __asm__("rsp");
  register __UINT64_TYPE__ rax __asm__("rax");

  // ABI requires that the return value should be stored in rax. So, we store
  // |val| in rax. Note that this has to happen before we restore the registers
  // from values in |buf|. Otherwise, once rsp and rbp are updated, we cannot
  // read |val|.
  val = val == 0 ? 1 : val;
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(rax) : "m"(val) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(rbx) : "m"(buf->rbx) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(rbp) : "m"(buf->rbp) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(r12) : "m"(buf->r12) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(r13) : "m"(buf->r13) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(r14) : "m"(buf->r14) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(r15) : "m"(buf->r15) :);
  LIBC_INLINE_ASM("mov %1, %0\n\t" : "=r"(rsp) : "m"(buf->rsp) :);
  LIBC_INLINE_ASM("jmp *%0\n\t" : : "m"(buf->rip));
}
#endif

} // namespace LIBC_NAMESPACE_DECL
