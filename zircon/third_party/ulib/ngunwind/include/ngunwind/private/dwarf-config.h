/* libunwind - a platform-independent unwind library
   Copyright (c) 2003, 2005 Hewlett-Packard Development Company, L.P.
   Copyright (C) 2008 CodeSourcery
   Copyright (C) 2012 Tommi Rantala <tt.rantala@gmail.com>
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

#ifndef dwarf_config_h
#define dwarf_config_h

#if UNW_TARGET_ARM

#define DWARF_NUM_PRESERVED_REGS        128

#define dwarf_to_unw_regnum(reg) (((reg) <= UNW_ARM_R15) ? (reg) : 0)

/****************************************************************************/

#elif UNW_TARGET_AARCH64

/* This matches the value used by GCC (see
   gcc/config/aarch64/aarch64.h:DWARF_FRAME_REGISTERS.  */
#define DWARF_NUM_PRESERVED_REGS        97

#define dwarf_to_unw_regnum(reg) (((reg) <= UNW_AARCH64_V31) ? (reg) : 0)

/****************************************************************************/

#elif UNW_TARGET_X86_64

#ifdef CONFIG_MSABI_SUPPORT
#define DWARF_NUM_PRESERVED_REGS        33
#else
#define DWARF_NUM_PRESERVED_REGS        17
#endif 

#define DWARF_REGNUM_MAP_LENGTH         DWARF_NUM_PRESERVED_REGS

/****************************************************************************/

#else
# error "Unsupported arch"
#endif

/* Convert a pointer to a dwarf_cursor structure to a pointer to
   unw_cursor_t.  */
#define dwarf_to_cursor(c)      ((unw_cursor_t *) (c))

typedef struct dwarf_loc
  {
    unw_word_t val;
    unw_word_t type;            /* see DWARF_LOC_TYPE_* macros.  */
  }
dwarf_loc_t;

#endif /* dwarf_config_h */
