// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_SYMBOL_TESTS_H_
#define SRC_LIB_ELFLDLTL_SYMBOL_TESTS_H_

#include <lib/elfldltl/symbol.h>

#include <string>
#include <vector>

#include "tests.h"

template <class Elf>
class TestSymtab {
 public:
  using Addr = typename Elf::Addr;
  using Half = typename Elf::Half;
  using Sym = typename Elf::Sym;

  uint32_t AddString(std::string_view str) {
    if (str.empty()) {
      return 0;
    }
    size_t offset = strtab_.size();
    strtab_ += str;
    strtab_ += '\0';
    return static_cast<uint32_t>(offset);
  }

  TestSymtab& AddSymbol(std::string_view name, Addr value, Addr size, elfldltl::ElfSymBind bind,
                        elfldltl::ElfSymType type, Half shndx) {
    Sym sym{};
    sym.name = AddString(name);
    sym.value = value;
    sym.size = size;
    sym.info = static_cast<uint8_t>((static_cast<uint8_t>(bind) << 4) |  //
                                    (static_cast<uint8_t>(type) << 0));
    sym.shndx = shndx;
    symtab_.push_back(sym);
    return *this;
  }

  void SetInfo(elfldltl::SymbolInfo<Elf>& si) const {
    si.set_symtab(symtab());
    si.set_strtab(strtab());
  }

  cpp20::span<const Sym> symtab() const { return {symtab_.data(), symtab_.size()}; }

  std::string_view strtab() const { return strtab_; }

 private:
  std::vector<Sym> symtab_{{}};
  std::string strtab_{'\0', 1};
};

#endif  // SRC_LIB_ELFLDLTL_SYMBOL_TESTS_H_
