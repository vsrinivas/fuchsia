/*
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "symtab.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "garnet/lib/debugger_utils/elf_reader.h"
#include "garnet/lib/debugger_utils/elf_symtab.h"
#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/logging.h"

#include "third_party/processor-trace/libipt/include/intel-pt.h"

namespace simple_pt {

SymbolTable::SymbolTable(debugserver::ElfReader* reader,
                         const std::string& contents,
                         uint64_t cr3,
                         uint64_t base,
                         uint64_t offset,
                         bool is_kernel)
    : debugserver::ElfSymbolTable(reader->file_name(), contents),
      cr3_(cr3),
      base_(base),
      end_(0),
      offset_(offset),
      is_kernel_(is_kernel) {}

const Symbol* SymbolTable::FindSymbol(uint64_t addr) const {
  if (addr < base_ || addr >= end_)
    return nullptr;
  return debugserver::ElfSymbolTable::FindSymbol(addr - offset_);
}

static bool Cr3Matches(uint64_t cr3_1, uint64_t cr3_2) {
  // TODO(dje): Zero was used early on to mean "match everything".
  // pt_asid_no_cr3 currently serves that purpose too. Both currently mean
  // the same thing for backwards compatibility, but it may be useful to
  // distinguish "match everything" vs "match nothing".
  if (cr3_1 == 0 || cr3_2 == 0)
    return true;
  if (cr3_1 == pt_asid_no_cr3 || cr3_2 == pt_asid_no_cr3)
    return true;
  return cr3_1 == cr3_2;
}

const SymbolTable* FindSymbolTable(const SymbolTableTable& symtabs,
                                   uint64_t cr3, uint64_t pc) {
  for (auto& st : symtabs) {
    if (!Cr3Matches(st->cr3(), cr3))
      continue;
    if (pc < st->base() || pc >= st->end())
      continue;
    return st.get();
  }

  return nullptr;
}

const Symbol* FindSymbol(const SymbolTableTable& symtabs,
                         uint64_t cr3, uint64_t pc,
                         const SymbolTable** out_symtab) {
  const SymbolTable* symtab = FindSymbolTable(symtabs, cr3, pc);
  if (!symtab) {
    *out_symtab = nullptr;
    return nullptr;
  }
  *out_symtab = symtab;
  return symtab->FindSymbol(pc);
}

const char* FindPcFileName(const SymbolTableTable& symtabs,
                           uint64_t cr3, uint64_t pc) {
  for (const auto& st : symtabs) {
    if (!Cr3Matches(st->cr3(), cr3))
      continue;
    if (pc < st->base() || pc >= st->end())
      continue;
    return st->file_name().c_str();
  }
  return nullptr;
}

bool SeenCr3(const SymbolTableTable& symtabs, uint64_t cr3) {
  for (const auto& st : symtabs) {
    if (st->cr3() == cr3)
      return true;
  }
  return false;
}

}  // simple_pt
