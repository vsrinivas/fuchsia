/* libunwind - a platform-independent unwind library
   Copyright (C) 2001-2005 Hewlett-Packard Co
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

#ifndef ARM_LIBUNWIND_I_H
#define ARM_LIBUNWIND_I_H

/* Target-dependent definitions that are internal to libunwind but need
   to be shared with target-independent code.  */

#include "elf32.h"

typedef enum
  {
    UNW_ARM_FRAME_STANDARD = -2,  /* regular r7, sp +/- offset */
    UNW_ARM_FRAME_SIGRETURN = -1, /* special sigreturn frame */
    UNW_ARM_FRAME_OTHER = 0,      /* not cacheable (special or unrecognised) */
    UNW_ARM_FRAME_GUESSED = 1     /* guessed it was regular, but not known */
  }
unw_tdep_frame_type_t;

typedef enum
  {
    ARM_SCF_NONE,
    ARM_SCF_LINUX_RT_SIGFRAME,
  }
unw_tdep_sigcontext_format_t;

#endif /* ARM_LIBUNWIND_I_H */
