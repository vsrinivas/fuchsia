/* libunwind - a platform-independent unwind library
   Copyright (C) 2010, 2011 by FERMI NATIONAL ACCELERATOR LABORATORY

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
#include "offsets.h"

HIDDEN void
tdep_stash_frame (struct dwarf_cursor *d, struct dwarf_reg_state *rs)
{
  struct cursor *c = (struct cursor *) dwarf_to_cursor (d);
  unw_tdep_frame_t *f = &c->frame_info;

  Debug (4, "ip=0x%lx cfa=0x%lx type %d cfa [where=%d val=%ld] cfaoff=%ld"
         " ra=0x%lx rbp [where=%d val=%ld @0x%lx] rsp [where=%d val=%ld @0x%lx]\n",
         d->ip, d->cfa, f->frame_type,
         rs->reg[DWARF_CFA_REG_COLUMN].where,
         rs->reg[DWARF_CFA_REG_COLUMN].val,
         rs->reg[DWARF_CFA_OFF_COLUMN].val,
         DWARF_GET_LOC(d->loc[d->ret_addr_column]),
         rs->reg[RBP].where, rs->reg[RBP].val, DWARF_GET_LOC(d->loc[RBP]),
         rs->reg[RSP].where, rs->reg[RSP].val, DWARF_GET_LOC(d->loc[RSP]));

  /* A standard frame is defined as:
      - CFA is register-relative offset off RBP or RSP;
      - Return address is saved at CFA-8;
      - RBP is unsaved or saved at CFA+offset, offset != -1;
      - RSP is unsaved or saved at CFA+offset, offset != -1.  */
  if (f->frame_type == UNW_X86_64_FRAME_OTHER
      && (rs->reg[DWARF_CFA_REG_COLUMN].where == DWARF_WHERE_REG)
      && (rs->reg[DWARF_CFA_REG_COLUMN].val == RBP
          || rs->reg[DWARF_CFA_REG_COLUMN].val == RSP)
      && labs((long) rs->reg[DWARF_CFA_OFF_COLUMN].val) < (1 << 29)
      && DWARF_GET_LOC(d->loc[d->ret_addr_column]) == d->cfa-8
      && (rs->reg[RBP].where == DWARF_WHERE_UNDEF
          || rs->reg[RBP].where == DWARF_WHERE_SAME
          || (rs->reg[RBP].where == DWARF_WHERE_CFAREL
              && labs((long) rs->reg[RBP].val) < (1 << 14)
              && rs->reg[RBP].val+1 != 0))
      && (rs->reg[RSP].where == DWARF_WHERE_UNDEF
          || rs->reg[RSP].where == DWARF_WHERE_SAME
          || (rs->reg[RSP].where == DWARF_WHERE_CFAREL
              && labs((long) rs->reg[RSP].val) < (1 << 14)
              && rs->reg[RSP].val+1 != 0)))
  {
    /* Save information for a standard frame. */
    f->frame_type = UNW_X86_64_FRAME_STANDARD;
    f->cfa_reg_sp = (rs->reg[DWARF_CFA_REG_COLUMN].val == RSP);
    f->cfa_reg_offset = rs->reg[DWARF_CFA_OFF_COLUMN].val;
    if (rs->reg[RBP].where == DWARF_WHERE_CFAREL)
      f->fp_cfa_offset = rs->reg[RBP].val;
    if (rs->reg[RSP].where == DWARF_WHERE_CFAREL)
      f->sp_cfa_offset = rs->reg[RSP].val;
    Debug (4, " standard frame\n");
  }

#ifdef __linux__
  /* Signal frame was detected via augmentation in tdep_fetch_frame()  */
  else if (f->frame_type == UNW_X86_64_FRAME_SIGRETURN)
  {
    /* Later we are going to fish out {RBP,RSP,RIP} from sigcontext via
       their ucontext_t offsets.  Confirm DWARF info agrees with the
       offsets we expect.  */

#ifndef NDEBUG
    const unw_word_t uc = c->sigcontext_addr;

    assert (DWARF_GET_LOC(d->loc[RIP]) - uc == UC_MCONTEXT_GREGS_RIP);
    assert (DWARF_GET_LOC(d->loc[RBP]) - uc == UC_MCONTEXT_GREGS_RBP);
    assert (DWARF_GET_LOC(d->loc[RSP]) - uc == UC_MCONTEXT_GREGS_RSP);
#endif

    Debug (4, " sigreturn frame\n");
  }
#endif

  /* PLT and guessed RBP-walked frames are handled in unw_step(). */
  else
  {
    Debug (4, " unusual frame\n");
  }
}

#ifdef __linux__

HIDDEN void
tdep_fetch_frame (struct dwarf_cursor *dw, unw_word_t ip, int need_unwind_info)
{
  struct cursor *c = (struct cursor *) dw;
  assert(! need_unwind_info || dw->pi_valid);
  assert(! need_unwind_info || dw->pi.unwind_info);
  if (dw->pi_valid
      && dw->pi.unwind_info
      && ((struct dwarf_cie_info *) dw->pi.unwind_info)->signal_frame)
    c->sigcontext_format = X86_64_SCF_LINUX_RT_SIGFRAME;
  else
    c->sigcontext_format = X86_64_SCF_NONE;

  Debug(5, "fetch frame ip=0x%lx cfa=0x%lx format=%d\n",
        dw->ip, dw->cfa, c->sigcontext_format);
}

HIDDEN void
tdep_cache_frame (struct dwarf_cursor *dw, struct dwarf_reg_state *rs)
{
  struct cursor *c = (struct cursor *) dw;
  rs->signal_frame = c->sigcontext_format;

  Debug(5, "cache frame ip=0x%lx cfa=0x%lx format=%d\n",
        dw->ip, dw->cfa, c->sigcontext_format);
}

HIDDEN void
tdep_reuse_frame (struct dwarf_cursor *dw, struct dwarf_reg_state *rs)
{
  struct cursor *c = (struct cursor *) dw;
  c->sigcontext_format = rs->signal_frame;
  if (c->sigcontext_format == X86_64_SCF_LINUX_RT_SIGFRAME)
  {
    c->frame_info.frame_type = UNW_X86_64_FRAME_SIGRETURN;
    /* Offset from cfa to ucontext_t in signal frame.  */
    c->frame_info.cfa_reg_offset = 0;
    c->sigcontext_addr = dw->cfa;
  }
  else
    c->sigcontext_addr = 0;

  Debug(5, "reuse frame ip=0x%lx cfa=0x%lx format=%d addr=0x%lx offset=%+d\n",
        dw->ip, dw->cfa, c->sigcontext_format, c->sigcontext_addr,
        (c->sigcontext_format == X86_64_SCF_LINUX_RT_SIGFRAME
         ? c->frame_info.cfa_reg_offset : 0));
}

#endif
