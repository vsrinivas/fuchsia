/* libunwind - a platform-independent unwind library
   Copyright (C) 2008 CodeSourcery
   Copyright (C) 2011-2013 Linaro Limited
   Copyright (C) 2012 Tommi Rantala <tt.rantala@gmail.com>

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
#include "dwarf_i.h"

PROTECTED int
unw_step (unw_cursor_t *cursor)
{
  struct cursor *c = (struct cursor *) cursor;
  int ret;

  Debug (1, "(cursor=%p, ip=0x%lx, cfa=0x%lx))\n",
         c, c->dwarf.ip, c->dwarf.cfa);

  /* Check if this is a signal frame. */
  ret = unw_is_signal_frame (cursor);
  if (ret < 0)
    {
      Debug (2, "returning %d\n", ret);
      return ret;
    }
  if (ret)
    {
      ret = unw_handle_signal_frame (cursor);
      Debug (2, "returning %d\n", ret);
      return ret;
    }

  ret = dwarf_step (&c->dwarf);
  Debug(1, "dwarf_step()=%d\n", ret);

  if (unlikely (ret == -UNW_ESTOPUNWIND))
    {
      Debug (2, "returning %d\n", ret);
      return ret;
    }

  if (ret < 0 && ret != -UNW_ENOINFO)
    {
      Debug (2, "returning %d\n", 0);
      return 0;
    }

  if (ret >= 0)
    {
      ret = (c->dwarf.ip == 0) ? 0 : 1;
    }
  else // ret == -UNW_ENOINFO
    {
      // If there's no unwind info fall back to a heuristic.
      // TODO: Make configurable? unw_step_etc?
      // Note: This is copied from x86_64.
      // TODO: arm64 has a canonical frame pointer: r29. But it's not clear
      // yet whether the heuristics that x86_64/Gstep.c employees can just be
      // carried over.

      unw_word_t prev_ip = c->dwarf.ip, prev_cfa = c->dwarf.cfa;
      struct dwarf_loc sp_loc, pc_loc;

      // We could get here because of missing/bad unwind information.
      // Validate all addresses from now on before dereferencing.
      c->validate = 1;

      Debug (13, "dwarf_step() failed (ret=%d), trying frame-chain\n", ret);

      if (DWARF_IS_NULL_LOC (c->dwarf.loc[UNW_AARCH64_SP]))
        {
          for (int i = 0; i < DWARF_NUM_PRESERVED_REGS; ++i)
            c->dwarf.loc[i] = DWARF_NULL_LOC;
        }
      else
        {
          unw_word_t sp;

          ret = dwarf_get (&c->dwarf, c->dwarf.loc[UNW_AARCH64_SP], &sp);
          if (ret < 0)
            {
              Debug (2, "returning %d [SP=0x%lx]\n", ret,
                     DWARF_GET_LOC (c->dwarf.loc[UNW_AARCH64_SP]));
              return ret;
            }

          if (!sp)
            {
              /* Looks like we may have reached the end of the call-chain.  */
              sp_loc = DWARF_NULL_LOC;
              pc_loc = DWARF_NULL_LOC;
            }
          else
            {
              unw_word_t sp1 = 0;
              sp_loc = DWARF_LOC (sp, 0);
              pc_loc = DWARF_LOC (sp + 8, 0);
              ret = dwarf_get (&c->dwarf, sp_loc, &sp1);
              Debug (1, "[SP=0x%lx] = 0x%lx (cfa = 0x%lx) -> 0x%lx\n",
                     (unsigned long) DWARF_GET_LOC (c->dwarf.loc[UNW_AARCH64_SP]),
                     sp, c->dwarf.cfa, sp1);

#if 0 // TODO: wip
              /* Heuristic to determine incorrect guess.  For SP to be a
                 valid frame it needs to be above current CFA, but don't
                 let it go more than a little.  Note that we can't deduce
                 anything about new SP (sp1) since it may not be a frame
                 pointer in the frame above.  Just check we get the value. */
              if (ret < 0
                  || rbp < c->dwarf.cfa
                  || (rbp - c->dwarf.cfa) > 0x4000)
                {
                  pc_loc = DWARF_NULL_LOC;
                  sp_loc = DWARF_NULL_LOC;
                }
#endif

              c->frame_info.frame_type = UNW_AARCH64_FRAME_GUESSED;
              c->frame_info.cfa_reg_sp = 0;
              c->frame_info.cfa_reg_offset = 16;
              c->frame_info.sp_cfa_offset = -16;
              c->dwarf.cfa += 16;
            }

          /* Mark all registers unsaved */
          for (int i = 0; i < DWARF_NUM_PRESERVED_REGS; ++i)
            c->dwarf.loc[i] = DWARF_NULL_LOC;

          c->dwarf.loc[UNW_AARCH64_SP] = sp_loc;
          c->dwarf.loc[UNW_AARCH64_PC] = pc_loc;
          c->dwarf.use_prev_instr = 1;
        }

      c->dwarf.ret_addr_column = UNW_AARCH64_PC;

      if (DWARF_IS_NULL_LOC (c->dwarf.loc[UNW_AARCH64_SP]))
        {
          ret = 0;
          Debug (2, "NULL %%sp loc, returning %d\n", ret);
          return ret;
        }
      if (!DWARF_IS_NULL_LOC (c->dwarf.loc[UNW_AARCH64_PC]))
        {
          ret = dwarf_get (&c->dwarf, c->dwarf.loc[UNW_AARCH64_PC], &c->dwarf.ip);
          Debug (1, "Frame Chain [PC=0x%Lx] = 0x%Lx\n",
                     (unsigned long long) DWARF_GET_LOC (c->dwarf.loc[UNW_AARCH64_PC]),
                     (unsigned long long) c->dwarf.ip);
          if (ret < 0)
            {
              Debug (2, "returning %d\n", ret);
              return ret;
            }
          ret = 1;
        }
      else
        {
          c->dwarf.ip = 0;
          ret = 0;
        }

      if (c->dwarf.ip == prev_ip && c->dwarf.cfa == prev_cfa)
        {
          ret = -UNW_EBADFRAME;
          Debug (2, "returning %d\n", ret);
          return ret;
        }
    }

  Debug (2, "returning %d\n", ret);
  return ret;
}
