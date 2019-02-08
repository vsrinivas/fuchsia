// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>

#include <lib/fxl/logging.h>

#include "garnet/lib/elflib/elflib.h"

namespace elflib {
namespace {

// Pull a null-terminated string out of an array of bytes at an offset. Returns
// empty string if there is no null terminator.
std::string GetNullTerminatedStringAt(const uint8_t* data, size_t data_length,
                                      size_t offset) {
  size_t check = offset;

  while (check < data_length && data[check]) {
    check++;
  }

  if (check >= data_length) {
    return std::string();
  }

  const char* start = reinterpret_cast<const char*>(data) + offset;

  return std::string(start);
}

}  // namespace

std::optional<Elf64_Ehdr> ElfLib::MemoryAccessorForFile::GetHeader() {
  auto data = GetMemory(0, sizeof(Elf64_Ehdr));

  if (!data) {
    return std::nullopt;
  }

  return *reinterpret_cast<const Elf64_Ehdr*>(data);
}

std::optional<std::vector<Elf64_Shdr>>
ElfLib::MemoryAccessorForFile::GetSectionHeaders(uint64_t offset,
                                                 size_t count) {
  auto data = GetMemory(offset, sizeof(Elf64_Shdr) * count);

  if (!data) {
    return std::nullopt;
  }

  auto array = reinterpret_cast<const Elf64_Shdr*>(data);

  return std::vector<Elf64_Shdr>(array, array + count);
}

std::optional<std::vector<Elf64_Phdr>>
ElfLib::MemoryAccessorForFile::GetProgramHeaders(uint64_t offset,
                                                 size_t count) {
  auto data = GetMemory(offset, sizeof(Elf64_Phdr) * count);

  if (!data) {
    return std::nullopt;
  }

  auto array = reinterpret_cast<const Elf64_Phdr*>(data);

  return std::vector<Elf64_Phdr>(array, array + count);
}

ElfLib::ElfLib(std::unique_ptr<MemoryAccessor>&& memory)
    : memory_(std::move(memory)) {}

ElfLib::~ElfLib() = default;

std::unique_ptr<ElfLib> ElfLib::Create(
    std::unique_ptr<MemoryAccessor>&& memory) {
  std::unique_ptr<ElfLib> out = std::make_unique<ElfLib>(std::move(memory));

  auto header = out->memory_->GetHeader();

  if (!header) {
    return std::unique_ptr<ElfLib>();
  }

  out->header_ = *header;

  // We don't support non-standard section header sizes. Stripped binaries that
  // don't have sections sometimes zero out the shentsize, so we can ignore it
  // if we have no sections.
  if (out->header_.e_shnum > 0 &&
      out->header_.e_shentsize != sizeof(Elf64_Shdr)) {
    return std::unique_ptr<ElfLib>();
  }

  // We don't support non-standard program header sizes.
  if (out->header_.e_phentsize != sizeof(Elf64_Phdr)) {
    return std::unique_ptr<ElfLib>();
  }

  return out;
}

const Elf64_Shdr* ElfLib::GetSectionHeader(size_t section) {
  if (sections_.empty()) {
    auto sections =
        memory_->GetSectionHeaders(header_.e_shoff, header_.e_shnum);

    if (!sections) {
      return nullptr;
    }

    sections_ = *sections;
  }

  if (section >= sections_.size()) {
    return nullptr;
  }

  return &sections_[section];
}

bool ElfLib::LoadProgramHeaders() {
  if (!segments_.empty()) {
    return true;
  }

  auto segments = memory_->GetProgramHeaders(header_.e_phoff, header_.e_phnum);

  if (!segments) {
    return false;
  }

  segments_ = *segments;
  return true;
}

ElfLib::MemoryRegion ElfLib::GetSegmentData(size_t segment) {
  LoadProgramHeaders();

  if (segment > segments_.size()) {
    return {};
  }

  const Elf64_Phdr* header = &segments_[segment];

  return memory_->GetLoadableMemory(header->p_offset, header->p_vaddr,
                                    header->p_filesz, header->p_memsz);
}

std::optional<std::vector<uint8_t>> ElfLib::GetNote(const std::string& name,
                                                    uint64_t type) {
  LoadProgramHeaders();

  for (size_t idx = 0; idx < segments_.size(); idx++) {
    if (segments_[idx].p_type != PT_NOTE) {
      continue;
    }

    auto data = GetSegmentData(idx);

    const Elf64_Nhdr* header;
    size_t namesz_padded;
    size_t descsz_padded;

    for (const uint8_t* pos = data.ptr; pos < data.ptr + data.size;
         pos += sizeof(Elf64_Nhdr) + namesz_padded + descsz_padded) {
      header = reinterpret_cast<const Elf64_Nhdr*>(pos);
      namesz_padded = (header->n_namesz + 3) & ~3UL;
      descsz_padded = (header->n_descsz + 3) & ~3UL;

      if (header->n_type != type) {
        continue;
      }

      auto name_data = pos + sizeof(Elf64_Nhdr);
      std::string entry_name(reinterpret_cast<const char*>(name_data),
                             header->n_namesz - 1);

      if (entry_name == name) {
        auto desc_data = name_data + namesz_padded;

        return std::vector(desc_data, desc_data + header->n_descsz);
      }
    }
  }

  return std::nullopt;
}

ElfLib::MemoryRegion ElfLib::GetSectionData(size_t section) {
  const Elf64_Shdr* header = GetSectionHeader(section);

  if (!header) {
    return {};
  }

  return memory_->GetLoadableMemory(header->sh_offset, header->sh_addr,
                                    header->sh_size, header->sh_size);
}

ElfLib::MemoryRegion ElfLib::GetSectionData(const std::string& name) {
  if (section_names_.size() == 0) {
    auto section_name_data = GetSectionData(header_.e_shstrndx);

    if (!section_name_data.ptr) {
      return {};
    }

    size_t idx = 0;
    // We know sections_ is populated from the GetSectionData above
    for (const auto& section : sections_) {
      auto name = GetNullTerminatedStringAt(
          section_name_data.ptr, section_name_data.size, section.sh_name);
      section_names_[name] = idx;

      idx++;
    }
  }

  const auto& iter = section_names_.find(name);

  if (iter == section_names_.end()) {
    return {};
  }

  return GetSectionData(iter->second);
}

bool ElfLib::LoadDynamicSymbols() {
  if (dynamic_symtab_offset_ || dynamic_strtab_offset_) {
    return true;
  }

  LoadProgramHeaders();

  for (size_t idx = 0; idx < segments_.size(); idx++) {
    if (segments_[idx].p_type != PT_DYNAMIC) {
      continue;
    }

    auto data = GetSegmentData(idx);

    if (!data.ptr) {
      return false;
    }

    const Elf64_Dyn* start = reinterpret_cast<const Elf64_Dyn*>(data.ptr);
    const Elf64_Dyn* end = start + (data.size / sizeof(Elf64_Dyn));

    dynamic_strtab_size_ = 0;
    dynamic_symtab_size_ = 0;

    for (auto dyn = start; dyn != end; dyn++) {
      if (dyn->d_tag == DT_STRTAB) {
        if (dynamic_strtab_offset_) {
          // We have more than one entry specifying the strtab location. Not
          // clear what to do there so just ignore all but the first.
          continue;
        }

        dynamic_strtab_offset_ = dyn->d_un.d_ptr;
      } else if (dyn->d_tag == DT_SYMTAB) {
        if (dynamic_symtab_offset_) {
          continue;
        }

        dynamic_symtab_offset_ = dyn->d_un.d_ptr;
      } else if (dyn->d_tag == DT_STRSZ) {
        if (dynamic_strtab_size_) {
          continue;
        }

        dynamic_strtab_size_ = dyn->d_un.d_val;
      } else if (dyn->d_tag == DT_HASH) {
        // A note: The old DT_HASH style of hash table is considered legacy on
        // Fuchsia. Technically a binary could provide both styles of hash
        // table and we can produce a sane result in that case, so this code
        // ignores DT_HASH.
        FXL_LOG(WARNING) << "Old style DT_HASH table found.";
      } else if (dyn->d_tag == DT_GNU_HASH) {
        auto addr = dyn->d_un.d_ptr;

        // Our elf header doesn't provide the DT_GNU_HASH header structure.
        struct Header {
          uint32_t nbuckets;
          uint32_t symoffset;
          uint32_t bloom_size;
          uint32_t bloom_shift;
        } header;

        static_assert(sizeof(Header) == 16);

        auto data = memory_->GetLoadedMemory(addr, sizeof(header));

        if (!data) {
          continue;
        }

        header = *reinterpret_cast<const Header*>(data);

        addr += sizeof(header);
        addr += 8 * header.bloom_size;

        size_t bucket_bytes = 4 * header.nbuckets;
        auto bucket_data = memory_->GetLoadedMemory(addr, bucket_bytes);

        if (!bucket_data) {
          continue;
        }

        const uint32_t* buckets =
            reinterpret_cast<const uint32_t*>(bucket_data);
        uint32_t max_bucket =
            *std::max_element(buckets, buckets + header.nbuckets);

        if (max_bucket < header.symoffset) {
          dynamic_symtab_size_ = max_bucket;
          continue;
        }

        addr += bucket_bytes;
        addr += (max_bucket - header.symoffset) * 4;

        for (uint32_t nsyms = max_bucket + 1;; nsyms++, addr += 4) {
          auto chain_entry_data = memory_->GetLoadedMemory(addr, 4);

          if (!chain_entry_data) {
            break;
          }

          uint32_t chain_entry =
              *reinterpret_cast<const uint32_t*>(chain_entry_data);

          if (chain_entry & 1) {
            dynamic_symtab_size_ = nsyms;
            break;
          }
        }
      }
    }

    return true;
  }

  return false;
}

std::optional<std::string> ElfLib::GetString(size_t index) {
  auto string_data = GetSectionData(".strtab");

  if (!string_data.ptr) {
    if (!LoadDynamicSymbols()) {
      return std::nullopt;
    }

    auto data =
        memory_->GetLoadedMemory(*dynamic_strtab_offset_, dynamic_strtab_size_);

    if (!data) {
      return std::nullopt;
    }

    string_data =
        ElfLib::MemoryRegion{.ptr = data, .size = dynamic_strtab_size_};
  }

  return GetNullTerminatedStringAt(string_data.ptr, string_data.size, index);
}

std::pair<const Elf64_Sym*, size_t> ElfLib::GetSymtab() {
  ElfLib::MemoryRegion symtab = GetSectionData(".symtab");

  if (symtab.ptr) {
    const Elf64_Sym* symbols = reinterpret_cast<const Elf64_Sym*>(symtab.ptr);

    return std::make_pair(symbols, symtab.size / sizeof(Elf64_Sym));
  }

  if (!LoadDynamicSymbols()) {
    return std::make_pair(nullptr, 0);
  }

  if (!dynamic_symtab_offset_) {
    return std::make_pair(nullptr, 0);
  }
  auto memory = memory_->GetLoadedMemory(
      *dynamic_symtab_offset_, dynamic_symtab_size_ * sizeof(Elf64_Sym));

  return std::make_pair(reinterpret_cast<const Elf64_Sym*>(memory),
                        dynamic_symtab_size_);
}

const Elf64_Sym* ElfLib::GetSymbol(const std::string& name) {
  auto symtab = GetSymtab();

  if (!symtab.first) {
    return nullptr;
  }

  const Elf64_Sym* symbols = symtab.first;
  const Elf64_Sym* end = symtab.first + symtab.second;

  for (auto symbol = symbols; symbol <= end; symbol++) {
    auto got_name = GetString(symbol->st_name);

    if (got_name && *got_name == name) {
      return symbol;
    }
  }

  return nullptr;
}

std::optional<std::map<std::string, Elf64_Sym>> ElfLib::GetAllSymbols() {
  auto symtab = GetSymtab();

  if (!symtab.first) {
    return std::nullopt;
  }

  std::map<std::string, Elf64_Sym> out;

  const Elf64_Sym* symbols = symtab.first;
  const Elf64_Sym* end = symtab.first + symtab.second;

  for (auto symbol = symbols; symbol != end; symbol++) {
    auto got_name = GetString(symbol->st_name);

    if (got_name) {
      out[*got_name] = *symbol;
    }
  }

  return out;
}

std::optional<uint64_t> ElfLib::GetSymbolValue(const std::string& name) {
  const Elf64_Sym* sym = GetSymbol(name);

  if (sym) {
    return sym->st_value;
  }

  return std::nullopt;
}

}  // namespace elflib
