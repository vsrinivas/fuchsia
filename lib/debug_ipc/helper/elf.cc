// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/elf.h"

namespace debug_ipc {

namespace {

constexpr size_t kMaxBuildIDSize = 64;

// This defines just enough of the ELF structures to compile the below code.
// <elf.h> doesn't exist on Mac.

#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4

#define PT_NOTE 4

#define NT_GNU_BUILD_ID 3

using Elf64_Half = uint16_t;
using Elf64_Word = uint32_t;
using Elf64_Xword = uint64_t;
using Elf64_Addr = uint64_t;
using Elf64_Off = uint64_t;

struct Elf64_Ehdr {
  unsigned char e_ident[EI_NIDENT];
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
};

struct Elf64_Phdr {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;
  Elf64_Addr p_vaddr;
  Elf64_Addr p_paddr;
  Elf64_Xword p_filesz;
  Elf64_Xword p_memsz;
  Elf64_Xword p_align;
};

struct Elf64_Nhdr {
  Elf64_Word n_namesz;
  Elf64_Word n_descsz;
  Elf64_Word n_type;
};

}  // namespace

std::string ExtractBuildID(
    std::function<bool(uint64_t offset, void* buffer, size_t length)> read_fn) {
  // The buffer will hold a hex version of the build ID (2 chars per byte)
  // plus the null terminator (1 more).
  constexpr size_t buf_size = kMaxBuildIDSize * 2 + 1;
  char buf[buf_size];

  // ELF header.
  uint8_t header[SELFMAG];
  if (!read_fn(0, header, SELFMAG))
    return std::string();
  if (memcmp(header, ELFMAG, SELFMAG))
    return std::string();

  Elf64_Off phoff;
  Elf64_Half num;
  if (!read_fn(offsetof(Elf64_Ehdr, e_phoff), &phoff, sizeof(phoff)))
    return std::string();
  if (!read_fn(offsetof(Elf64_Ehdr, e_phnum), &num, sizeof(num)))
    return std::string();

  for (Elf64_Half n = 0; n < num; n++) {
    uint64_t phaddr = phoff + (n * sizeof(Elf64_Phdr));
    Elf64_Word type;
    if (!read_fn(phaddr + offsetof(Elf64_Phdr, p_type), &type, sizeof(type)))
      return std::string();
    if (type != PT_NOTE)
      continue;

    Elf64_Off off;
    Elf64_Xword size;
    if (!read_fn(phaddr + offsetof(Elf64_Phdr, p_offset), &off, sizeof(off)))
      return std::string();
    if (!read_fn(phaddr + offsetof(Elf64_Phdr, p_filesz), &size, sizeof(size)))
      return std::string();

    constexpr size_t kGnuSignatureSize = 4;
    const char kGnuSignature[kGnuSignatureSize] = "GNU";

    struct {
      Elf64_Nhdr hdr;
      char name[kGnuSignatureSize];
    } hdr;

    while (size > sizeof(hdr)) {
      if (!read_fn(off, &hdr, sizeof(hdr)))
        return std::string();
      size_t header_size = sizeof(Elf64_Nhdr) + ((hdr.hdr.n_namesz + 3) & -4);
      size_t payload_size = (hdr.hdr.n_descsz + 3) & -4;
      off += header_size;
      size -= header_size;
      uint64_t payload_vaddr = off;
      off += payload_size;
      size -= payload_size;
      if (hdr.hdr.n_type != NT_GNU_BUILD_ID ||
          hdr.hdr.n_namesz != kGnuSignatureSize ||
          memcmp(hdr.name, kGnuSignature, kGnuSignatureSize) != 0) {
        continue;
      }
      if (hdr.hdr.n_descsz > kMaxBuildIDSize) {
        return std::string();  // Too large.
      } else {
        uint8_t buildid[kMaxBuildIDSize];
        if (!read_fn(payload_vaddr, buildid, hdr.hdr.n_descsz))
          return std::string();
        for (uint32_t i = 0; i < hdr.hdr.n_descsz; ++i)
          snprintf(&buf[i * 2], 3, "%02x", buildid[i]);
      }
      return std::string(buf);
    }
  }

  return std::string();
}

std::string ExtractBuildID(FILE* file) {
  return ExtractBuildID([file](uint64_t offset, void* buffer, size_t length) {
    if (fseek(file, offset, SEEK_SET) != 0)
      return false;
    return fread(buffer, 1, length, file) == length;
  });
}

#if defined(__Fuchsia__)
std::string ExtractBuildID(const zx::process& process, uint64_t base) {
  return ExtractBuildID([&process, base](uint64_t offset, void* buffer,
                                         size_t length) {
    size_t num_read = 0;
    if (process.read_memory(base + offset, buffer, length, &num_read) != ZX_OK)
      return false;
    return num_read == length;
  });
}
#endif

}  // namespace debug_ipc
