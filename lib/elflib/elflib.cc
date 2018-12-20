// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "garnet/lib/elflib/elflib.h"

namespace elflib {

namespace {

// Load the standard ELF string table format
void LoadStringTable(const std::vector<uint8_t>& content,
                     std::vector<std::string>* strings) {
  const uint8_t* data = content.data();
  const uint8_t* end = data + content.size();

  while (data != end) {
    strings->emplace_back(reinterpret_cast<const char*>(data));
    data += strings->back().size() + 1;
  }
}

}  // namespace

ElfLib::ElfLib(std::unique_ptr<MemoryAccessor>&& memory)
    : memory_(std::move(memory)) {}

ElfLib::~ElfLib() = default;

std::unique_ptr<ElfLib> ElfLib::Create(
    std::unique_ptr<MemoryAccessor>&& memory) {
  std::unique_ptr<ElfLib> out = std::make_unique<ElfLib>(std::move(memory));
  std::vector<uint8_t> header_data;
  header_data.resize(sizeof(Elf64_Ehdr));

  if (!out->memory_->GetMemory(0, &header_data)) {
    return std::unique_ptr<ElfLib>();
  }

  out->header_ = *reinterpret_cast<Elf64_Ehdr*>(header_data.data());

  // We don't support non-standard section header sizes.
  if (out->header_.e_shentsize != sizeof(Elf64_Shdr)) {
    return std::unique_ptr<ElfLib>();
  }

  return out;
}

const Elf64_Shdr* ElfLib::GetSectionHeader(size_t section) {
  if (sections_.empty()) {
    std::vector<uint8_t> data;
    data.resize(sizeof(Elf64_Shdr) * header_.e_shnum);

    if (!memory_->GetMemory(header_.e_shoff, &data)) {
      return nullptr;
    }

    Elf64_Shdr* header_array = reinterpret_cast<Elf64_Shdr*>(data.data());

    std::copy(header_array, header_array + header_.e_shnum,
              std::back_inserter(sections_));
  }

  if (section >= sections_.size()) {
    return nullptr;
  }

  return &sections_[section];
}

const std::vector<uint8_t>* ElfLib::GetSectionData(size_t section) {
  const auto& iter = section_data_.find(section);
  if (iter != section_data_.end()) {
    return &iter->second;
  }

  const Elf64_Shdr* header = GetSectionHeader(section);

  if (!header) {
    return nullptr;
  }

  size_t count = header->sh_size;
  std::vector<uint8_t> data;
  data.resize(count);

  if (!memory_->GetMappedMemory(header->sh_offset, header->sh_addr, &data)) {
    return nullptr;
  }

  section_data_[section] = data;

  return &section_data_[section];
}

const std::vector<uint8_t>* ElfLib::GetSectionData(const std::string& name) {
  if (section_names_.size() == 0) {
    const std::vector<uint8_t>* section_name_data =
        GetSectionData(header_.e_shstrndx);

    if (!section_name_data) {
      return nullptr;
    }

    std::vector<std::string> strings;
    LoadStringTable(*section_name_data, &strings);

    size_t idx = 0;
    // We know sections_ is populated from the GetSectionData above
    for (const auto& section : sections_) {
      if (section.sh_name < strings.size()) {
        section_names_[strings[section.sh_name]] = idx;
      }

      idx++;
    }
  }

  const auto& iter = section_names_.find(name);

  if (iter == section_names_.end()) {
    return nullptr;
  }

  return GetSectionData(iter->second);
}

const std::string* ElfLib::GetString(size_t index) {
  if (strings_.empty()) {
    const std::vector<uint8_t>* string_data = GetSectionData(".strtab");

    if (!string_data) {
      return nullptr;
    }

    LoadStringTable(*string_data, &strings_);
  }

  if (index >= strings_.size()) {
    return nullptr;
  }

  return &strings_[index];
}

bool ElfLib::LoadSymbols() {
  if (symbols_.empty()) {
    const std::vector<uint8_t>* symbol_data = GetSectionData(".symtab");

    if (!symbol_data) {
      return false;
    }

    const Elf64_Sym* start =
        reinterpret_cast<const Elf64_Sym*>(symbol_data->data());
    const Elf64_Sym* end = start + (symbol_data->size() / sizeof(Elf64_Sym));
    std::copy(start, end, std::back_inserter(symbols_));
  }

  return true;
}

const Elf64_Sym* ElfLib::GetSymbol(const std::string& name) {
  if (!LoadSymbols()) {
    return nullptr;
  }

  for (const auto& symbol : symbols_) {
    const std::string* got_name = GetString(symbol.st_name);

    if (got_name != nullptr && *got_name == name) {
      return &symbol;
    }
  }

  return nullptr;
}

bool ElfLib::GetAllSymbols(std::map<std::string,Elf64_Sym>* out) {
  if (!LoadSymbols()) {
    return false;
  }

  for (const auto& symbol : symbols_) {
    const std::string* got_name = GetString(symbol.st_name);

    if (got_name != nullptr) {
      (*out)[*got_name] = symbol;
    }
  }

  return true;
}

bool ElfLib::GetSymbolValue(const std::string& name, uint64_t* out) {
  const Elf64_Sym* sym = GetSymbol(name);

  if (sym) {
    *out = sym->st_value;
  }

  return sym != nullptr;
}

}  // namespace elflib
