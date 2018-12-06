// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "garnet/lib/elflib/elflib.h"

namespace elflib {

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

    const uint8_t* data = section_name_data->data();
    const uint8_t* end = data + section_name_data->size();
    std::vector<std::string> strings;

    while (data != end) {
      strings.emplace_back(reinterpret_cast<const char*>(data));
      data += strings.back().size() + 1;
    }

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

}  // namespace elflib
