// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>

#include <lib/fxl/logging.h>

#include "src/lib/elflib/elflib.h"

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

  // Header magic should be correct.
  if (!std::equal(ElfMagic, ElfMagic + 4, out->header_.e_ident)) {
    return std::unique_ptr<ElfLib>();
  }

  // We only support 64-bit binaries.
  if (out->header_.e_ident[EI_CLASS] != ELFCLASS64) {
    return std::unique_ptr<ElfLib>();
  }

  const uint32_t kOne = 1;

  // Endianness of the file has to match the endianness of the host. To do the
  // endianness check, we snip the first byte off of a 4-byte word. If it
  // contains the LSB (a value of 1) we are on a little-endian machine.
  if (out->header_.e_ident[EI_DATA] == ELFDATA2MSB &&
      *reinterpret_cast<const char*>(&kOne)) {
    return std::unique_ptr<ElfLib>();
  }

  // Version field has only had one correct value for most of the life of the
  // spec.
  if (out->header_.e_ident[EI_VERSION] != EV_CURRENT) {
    return std::unique_ptr<ElfLib>();
  }

  if (out->header_.e_version != EV_CURRENT) {
    return std::unique_ptr<ElfLib>();
  }

  // We'll skip EI_OSABI and EI_ABIVERSION as well as e_machine and e_type. In
  // either case any valid value should be fine. We just don't screen for
  // invalid values.

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

bool ElfLib::LoadSectionNames() {
  if (section_names_.size() != 0) {
    return true;
  }

  auto section_name_data = GetSectionData(header_.e_shstrndx);

  if (!section_name_data.ptr) {
    return false;
  }

  size_t idx = 0;
  // We know sections_ is populated from the GetSectionData above
  for (const auto& section : sections_) {
    auto name = GetNullTerminatedStringAt(
        section_name_data.ptr, section_name_data.size, section.sh_name);
    section_names_[name] = idx;

    idx++;
  }

  return true;
}

ElfLib::MemoryRegion ElfLib::GetSectionData(const std::string& name) {
  if (!LoadSectionNames()) {
    return {};
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
      } else if (dyn->d_tag == DT_PLTREL) {
        dynamic_plt_use_rela_ = dyn->d_un.d_val == DT_RELA;
      }
    }

    return true;
  }

  return false;
}

std::map<std::string, uint64_t> ElfLib::GetPLTOffsets() {
  // We assume Fuchsia's defaults for each architecture. We could perhaps check
  // ELF_OSABI to firm up those assumptions. Fuchsia sets it to NONE.
  switch (header_.e_machine) {
    case EM_X86_64:
      return GetPLTOffsetsX64();
    default:
      FXL_LOG(WARNING) << "Architecture doesn't support GetPLTOffsets.";
      return {};
  }
}

std::map<std::string, uint64_t> ElfLib::GetPLTOffsetsX64() {
  // A PLT entry consists of 3 x86 instructions: a jump using a 6-byte
  // encoding, a push of one 32 bit value on to the stack, and another jump,
  // this one using a 5-byte encoding.
  //
  // We don't care about either of the jumps, but we want the value that is
  // pushed as it is the index into the relocation table which will tell us
  // what symbol this entry is for.
  struct PltEntry {
    char first_jump[6];
    char push_opcode;
    uint32_t index;
    char second_jump[5];
  } __attribute__((packed, aligned(1)));

  FXL_DCHECK(sizeof(PltEntry) == 16);

  // We'd prefer if this works but we can get by without it, so we're not
  // checking the return value.
  LoadDynamicSymbols();

  if (!LoadSectionNames()) {
    return {};
  }

  if (!dynamic_plt_use_rela_) {
    FXL_LOG(WARNING) << "Assuming Elf64_Rela PLT relocation format.";
  } else if (!*dynamic_plt_use_rela_) {
    FXL_LOG(WARNING) << "Elf64_Rel style PLT Relocations unsupported.";
    return {};
  }

  auto plt_section = section_names_.find(".plt");

  if (plt_section == section_names_.end()) {
    return {};
  }

  auto plt_idx = plt_section->second;

  auto plt_shdr = GetSectionHeader(plt_idx);
  auto plt_memory = GetSectionData(plt_idx);

  if (!plt_shdr || !plt_memory.ptr) {
    return {};
  }

  auto plt_load_addr = plt_shdr->sh_addr;

  auto plt = reinterpret_cast<const PltEntry*>(plt_memory.ptr);
  auto plt_end = plt + plt_memory.size / sizeof(PltEntry);

  auto reloc_memory = GetSectionData(".rela.plt");

  if (!reloc_memory.ptr) {
    return {};
  }

  auto reloc = reinterpret_cast<const Elf64_Rela*>(reloc_memory.ptr);
  auto reloc_count = reloc_memory.size / sizeof(Elf64_Rela);

  ElfLib::MemoryRegion dynsym_mem = GetSectionData(".dynsym");

  if (!dynsym_mem.ptr) {
    return {};
  }

  auto symtab = reinterpret_cast<const Elf64_Sym*>(dynsym_mem.ptr);
  auto sym_count = dynsym_mem.size / sizeof(Elf64_Sym);

  ElfLib::MemoryRegion dynstr_mem = GetSectionData(".dynstr");

  if (!dynstr_mem.ptr) {
    return {};
  }

  auto pos = plt + 1;  // First PLT entry is special. Ignore it.
  uint64_t idx = 1;

  std::map<std::string, uint64_t> ret;

  for (; pos != plt_end; pos++, idx++) {
    if (pos->push_opcode != 0x68) {
      FXL_LOG(WARNING) << "Push OpCode not found where expected in PLT.";
      continue;
    }

    if (pos->index >= reloc_count) {
      FXL_LOG(WARNING) << "PLT referenced reloc outside reloc table.";
      continue;
    }

    auto sym_idx = reloc[pos->index].getSymbol();

    if (sym_idx >= sym_count) {
      FXL_LOG(WARNING) << "PLT reloc referenced symbol outside symbol table.";
      continue;
    }

    auto name = GetNullTerminatedStringAt(dynstr_mem.ptr, dynstr_mem.size,
                                          symtab[sym_idx].st_name);

    if (!name.size()) {
      FXL_LOG(WARNING) << "PLT symbol name could not be retrieved.";
      continue;
    }

    ret[name] = idx * sizeof(PltEntry) + plt_load_addr;
  }

  return ret;
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
  auto [symtab_ptr, symtab_size] = GetSymtab();
  if (!symtab_ptr)
    return std::nullopt;

  std::map<std::string, Elf64_Sym> out;

  // We front-load the sym data, as it's the same memory section for each symbol
  // within the section.
  auto string_data = GetSectionData(".strtab");
  if (!string_data.ptr) {
    if (!LoadDynamicSymbols())
      return std::nullopt;

    auto data =
        memory_->GetLoadedMemory(*dynamic_strtab_offset_, dynamic_strtab_size_);
    string_data.ptr = data;
    string_data.size = dynamic_strtab_size_;
  }

  const Elf64_Sym* symbols = symtab_ptr;
  const Elf64_Sym* end = symtab_ptr + symtab_size;
  for (auto symbol = symbols; symbol != end; symbol++) {
    auto sym_name = GetNullTerminatedStringAt(string_data.ptr, string_data.size,
                                              symbol->st_name);
    out[sym_name] = *symbol;
  }

  return out;
}

}  // namespace elflib
