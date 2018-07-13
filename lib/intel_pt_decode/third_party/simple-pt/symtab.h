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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "garnet/lib/debugger_utils/elf_symtab.h"

namespace simple_pt {

using Symbol = debugger_utils::ElfSymbol;

class SymbolTable : public debugger_utils::ElfSymbolTable {
 public:
  SymbolTable(debugger_utils::ElfReader* reader,
              const std::string& contents,
              uint64_t cr3,
              uint64_t base,
              uint64_t offset,
              bool is_kernel);

  const Symbol* FindSymbol(uint64_t addr) const;

  uint64_t cr3() const { return cr3_; }
  uint64_t base() const { return base_; }
  uint64_t end() const { return end_; }
  uint64_t offset() const { return offset_; }
  bool is_kernel() const { return is_kernel_; }

  void set_end(uint64_t end) { end_ = end; }

 private:
  const uint64_t cr3_;
  const uint64_t base_;
  // This is computed after all symbols have been read in.
  uint64_t end_ = 0;
  const uint64_t offset_;
  const bool is_kernel_;
};

using SymbolTableTable = std::vector<std::unique_ptr<SymbolTable>>;

const SymbolTable* FindSymbolTable(const SymbolTableTable& symtabs,
                                   uint64_t cr3, uint64_t pc);

const Symbol* FindSymbol(const SymbolTableTable& symtabs,
                         uint64_t cr3, uint64_t pc,
                         const SymbolTable** out_symtab);

const char* FindPcFileName(const SymbolTableTable& symtabs,
                           uint64_t cr3, uint64_t pc);

bool SeenCr3(const SymbolTableTable& symtabs, uint64_t cr3);

}  // simple_pt
