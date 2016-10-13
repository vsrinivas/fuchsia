/* libunwind - a platform-independent unwind library
   Copyright (C) 2002-2003 Hewlett-Packard Co
        Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

   Modified for x86_64 by Max Asbock <masbock@us.ibm.com>

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

#include "unwind_i.h"

PROTECTED int
unw_is_signal_frame (unw_cursor_t *cursor)
{
#ifdef __linux__
  struct cursor *c = (struct cursor *) cursor;
  return c->sigcontext_format != X86_64_SCF_NONE;
#else
  return -UNW_ENOINFO;
#endif
}

PROTECTED int
unw_handle_signal_frame (unw_cursor_t *cursor)
{
#ifdef __linux__
#if UNW_DEBUG /* To silence compiler warnings */
  /* Should not get here because we now use kernel-provided dwarf
     information for the signal trampoline and dwarf_step() works.
     Hence unw_step() should never call this function. Maybe
     restore old non-dwarf signal handling here, but then the
     gating on unw_is_signal_frame() needs to be removed. */
  struct cursor *c = (struct cursor *) cursor;
  Debug(1, "old format signal frame? format=%d addr=0x%lx cfa=0x%lx\n",
        c->sigcontext_format, c->sigcontext_addr, c->dwarf.cfa);
#endif
  return -UNW_EBADFRAME;
#else
  return -UNW_EBADFRAME;
#endif
}
