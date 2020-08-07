/* libunwind - a platform-independent unwind library
   Copyright (C) 2001-2004 Hewlett-Packard Co
        Contributed by David Mosberger-Tang <davidm@hpl.hp.com>
   Copyright (C) 2013 Linaro Limited

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#ifndef LIBUNWIND_H
#define LIBUNWIND_H

#include <stdint.h>
#include <ucontext.h>

#define UNW_TARGET      riscv64
#define UNW_TARGET_RISCV64      1

/* This needs to be big enough to accommodate "struct cursor", while
   leaving some slack for future expansion.  Changing this value will
   require recompiling all users of this library.  Stack allocation is
   relatively cheap and unwind-state copying is relatively rare, so we
   want to err on making it rather too big than too small.
   As of this writing, 256 is enough so 512 provides a fair bit of room.
   The original value of 4096 is excessive.  */
#define UNW_TDEP_CURSOR_LEN     512

typedef uintptr_t unw_word_t;
typedef intptr_t unw_sword_t;

typedef long double unw_tdep_fpreg_t;

typedef enum
  {
    UNW_RISCV64_PLACEHOLDER,

    UNW_TDEP_LAST_REG = UNW_RISCV64_PLACEHOLDER,

    UNW_TDEP_IP = UNW_RISCV64_PLACEHOLDER,
    UNW_TDEP_SP = UNW_RISCV64_PLACEHOLDER,
    UNW_TDEP_EH = UNW_RISCV64_PLACEHOLDER,
  }
riscv64_regnum_t;

/* Use R0 through R3 to pass exception handling information.  */
#define UNW_TDEP_NUM_EH_REGS    4

/* On Riscv64, we can directly use ucontext_t as the unwind context.  */
typedef ucontext_t unw_tdep_context_t;

#endif /* LIBUNWIND_H */
