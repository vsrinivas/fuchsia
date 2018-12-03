// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "garnet/lib/elflib/elflib.h"

namespace elflib {

ElfLib::ElfLib(std::unique_ptr<MemoryAccessor>&& memory)
    : memory_(std::move(memory)) {}

ElfLib::~ElfLib() = default;

bool ElfLib::Create(std::unique_ptr<MemoryAccessor>&& memory,
                    std::unique_ptr<ElfLib> out) {
  std::make_unique<ElfLib>(std::move(memory));

  return out->GetDatum(0, &out->header_);
}

template <typename T>
bool ElfLib::GetDatum(uint64_t offset, T* out) {
  std::vector<uint8_t> data;
  data.resize(sizeof(T));

  if (!memory_->GetMemory(offset, &data)) {
    return false;
  }

  *out = *reinterpret_cast<T*>(data.data());
  return true;
}

template <typename T>
bool ElfLib::GetData(uint64_t offset, size_t count, std::vector<T>* out) {
  std::vector<uint8_t> data;
  data.resize(sizeof(T) * count);

  if (!memory_->GetMemory(offset, &data)) {
    return false;
  }

  T* out_array = reinterpret_cast<T*>(data.data());

  *out = std::vector(out_array, out_array + count);
  return true;
}

const Elf64_Shdr* ElfLib::GetSectionHeader(size_t section) {
  if (sections_.empty() &&
      !GetData(header_.e_shoff, header_.e_shnum, &sections_)) {
    return nullptr;
  }

  if (section >= sections_.size()) {
    return nullptr;
  }

  return &sections_[section];
}

template <typename T>
const std::vector<T>* ElfLib::GetSectionData(size_t section) {
  const auto& iter = section_data_.find(section);
  if (iter != section_data_.end()) {
    return &iter->second;
  }

  const Elf64_Shdr* header = GetSectionHeader(section);

  if (!header) {
    return nullptr;
  }

  size_t count = header->sh_size / sizeof(T);
  std::vector<T> data;
  data.resize(count);

  if (!GetData(header->sh_offset, count, &data)) {
    return nullptr;
  }

  section_data_[section] = data;

  return &section_data_[section];
}

bool ElfLib::GetSectionOffsetFromName(const std::string& name, size_t* out) {
  if (section_names_.size() == 0) {
    const std::vector<uint8_t>* section_name_data =
        GetSectionData<uint8_t>(header_.e_shstrndx);

    if (!section_name_data) {
      return false;
    }

    const auto* data = section_name_data->data();
    const auto* end = data + section_name_data->size();
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
    return false;
  }

  *out = iter->second;
  return true;
}

const Elf64_Shdr* ElfLib::GetSectionHeader(const std::string& name) {
  size_t off;

  if (!GetSectionOffsetFromName(name, &off)) {
    return nullptr;
  }

  return GetSectionHeader(off);
}

template <typename T>
const std::vector<T>* ElfLib::GetSectionData(const std::string& name) {
  size_t off;

  if (!GetSectionOffsetFromName(name, &off)) {
    return nullptr;
  }

  return GetSectionData<T>(off);
}

}  // namespace elflib
