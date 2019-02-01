// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "lib/fxl/macros.h"

#include "elf_reader.h"

namespace debugger_utils {

struct ElfSymbol {
  // weak pointer into the string section of the loaded ELF file
  const char* name = nullptr;
  uint64_t addr = 0;
  uint64_t size = 0;
};

class ElfSymbolTable {
 public:
  ElfSymbolTable(const std::string& file_name, const std::string& contents);
  ~ElfSymbolTable();

  const std::string& file_name() const { return file_name_; }
  const std::string& contents() const { return contents_; }
  size_t num_symbols() const { return num_symbols_; }

  // |symtab_type| is one of SHT_SYMTAB, SHT_DYNSYM.
  bool Populate(ElfReader* elf, unsigned symtab_type);

  // |index| must be valid.
  const ElfSymbol& GetSymbol(size_t index) const { return symbols_[index]; }

  const ElfSymbol* FindSymbol(uint64_t addr) const;

  void Dump(FILE*) const;

 private:
  void Finalize();

  // For debugging/informational purposes only.
  const std::string file_name_;

  // For debugging/informational purposes only.
  // One may wish to load a file's symbols into different symbol tables.
  // E.g., One may wish to keep SHT_SYMTAB and SHT_DYNSYM separate.
  // This string is for identifying the contents of the symtab in error
  // messages, etc.
  const std::string contents_;

  size_t num_symbols_ = 0;
  // Once Finalize() is called this is sorted by |Symbol.addr|.
  ElfSymbol* symbols_ = nullptr;

  // To separate our lifetime with that of the ELF reader, we store the
  // strings here.
  std::unique_ptr<ElfSectionContents> string_section_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ElfSymbolTable);
};

}  // namespace debugger_utils
