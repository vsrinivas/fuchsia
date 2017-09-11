// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "elf-symtab.h"

#include "lib/fxl/logging.h"

namespace debugserver {
namespace elf {

SymbolTable::SymbolTable(const std::string& file_name,
                         const std::string& contents)
    : file_name_(file_name), contents_(contents) {}

SymbolTable::~SymbolTable() {
  if (symbols_)
    delete[] symbols_;
}

bool SymbolTable::Populate(elf::Reader* elf, unsigned symtab_type) {
  FXL_DCHECK(symtab_type == SHT_SYMTAB || symtab_type == SHT_DYNSYM);

  // TODO(dje): Add support for loading both SHT_SYMTAB and SHT_DYNSYM.
  if (symbols_) {
    FXL_LOG(ERROR) << "Already populated";
    return false;
  }

  elf::Error rc = elf->ReadSectionHeaders();
  if (rc != Error::OK) {
    FXL_LOG(ERROR) << "Error reading ELF section headers: " << ErrorName(rc);
    return false;
  }

  const SectionHeader* shdr = elf->GetSectionHeaderByType(symtab_type);
  if (!shdr)
    return true;  // empty symbol table

  size_t num_sections = elf->GetNumSections();
  size_t string_section = shdr->sh_link;
  if (string_section >= num_sections) {
    FXL_LOG(ERROR) << "Bad string section: " << string_section;
    return false;
  }
  const SectionHeader& str_shdr = elf->GetSectionHeader(string_section);

  std::unique_ptr<SectionContents> contents;
  rc = elf->GetSectionContents(*shdr, &contents);
  if (rc != Error::OK) {
    FXL_LOG(ERROR) << "Error reading ELF section: " << ErrorName(rc);
    return false;
  }

  rc = elf->GetSectionContents(str_shdr, &string_section_);
  if (rc != Error::OK) {
    FXL_LOG(ERROR) << "Error reading ELF string section: " << ErrorName(rc);
    return false;
  }

  auto strings = reinterpret_cast<const char*>(string_section_->contents());
  size_t max_string_offset = string_section_->GetSize();

  size_t num_raw_symbols = contents->GetNumEntries();
  symbols_ = new Symbol[num_raw_symbols];

  size_t num_symbols = 0;
  for (size_t i = 0; i < num_raw_symbols; ++i) {
    const RawSymbol& sym = contents->GetSymbolEntry(i);
    if (sym.st_name >= max_string_offset) {
      FXL_LOG(ERROR) << "Bad symbol string name offset: " << sym.st_name;
      continue;
    }
    Symbol* s = &symbols_[num_symbols++];
    // TODO(dje): IWBN to have a convenience function for getting symbol
    // names, not sure what it will look like yet.
    s->name = strings + sym.st_name;
    s->addr = sym.st_value;
    s->size = sym.st_size;
  }

  num_symbols_ = num_symbols;
  Finalize();
  return true;
}

static int CompareSymbol(const void* ap, const void* bp) {
  auto a = reinterpret_cast<const Symbol*>(ap);
  auto b = reinterpret_cast<const Symbol*>(bp);
  if (a->addr >= b->addr && a->addr < b->addr + b->size)
    return 0;
  if (b->addr >= a->addr && b->addr < a->addr + a->size)
    return 0;
  return a->addr - b->addr;
}

void SymbolTable::Finalize() {
  qsort(symbols_, num_symbols_, sizeof(Symbol), CompareSymbol);
}

const Symbol* SymbolTable::FindSymbol(uint64_t addr) const {
  Symbol search = {.addr = addr};

  /* add last hit cache here */

  auto s = reinterpret_cast<const Symbol*>(
      bsearch(&search, symbols_, num_symbols_, sizeof(Symbol), CompareSymbol));
  return s;
}

void SymbolTable::Dump(FILE* f) const {
  fprintf(f, "file: %s\n", file_name_.c_str());
  fprintf(f, "contents: %s\n", contents_.c_str());
  for (size_t i = 0; i < num_symbols_; i++) {
    Symbol* s = &symbols_[i];
    if (s->addr && s->name[0])
      fprintf(f, "%p %s\n", (void*)s->addr, s->name);
  }
}

}  // namespace elf
}  // namespace debugserver
