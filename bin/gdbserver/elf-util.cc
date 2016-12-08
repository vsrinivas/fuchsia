// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "elf-util.h"

#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <magenta/status.h>
#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_printf.h"

#include "memory.h"
#include "util.h"

namespace debugserver {
namespace elf {

#if UINT_MAX == ULONG_MAX

typedef Elf32_Half elf_half_t;
typedef Elf32_Off elf_off_t;
// ELF used "word" for 32 bits, sigh.
typedef Elf32_Word elf_word_t;
typedef Elf32_Word elf_native_word_t;
typedef Elf32_Phdr elf_phdr_t;

#else

typedef Elf64_Half elf_half_t;
typedef Elf64_Off elf_off_t;
typedef Elf64_Word elf_word_t;
typedef Elf64_Xword elf_native_word_t;
typedef Elf64_Phdr elf_phdr_t;

#endif

#define ehdr_off_phoff offsetof(ehdr_type, e_phoff)
#define ehdr_off_phnum offsetof(ehdr_type, e_phnum)

#define phdr_off_type offsetof(phdr_type, p_type)
#define phdr_off_offset offsetof(phdr_type, p_offset)
#define phdr_off_filesz offsetof(phdr_type, p_filesz)

bool ReadElfHdr(const util::Memory& m, mx_vaddr_t base, ehdr_type* hdr) {
  return m.Read(base, hdr, sizeof(*hdr));
}

bool VerifyElfHdr(const ehdr_type* hdr) {
  if (memcmp(hdr->e_ident, ELFMAG, SELFMAG))
    return false;
  // TODO(dje): Could add more checks.
  return true;
}

bool ReadBuildId(const util::Memory& m,
                 mx_vaddr_t base,
                 const ehdr_type* hdr,
                 char* buf,
                 size_t buf_size) {
  mx_vaddr_t vaddr = base;

  FTL_DCHECK(buf_size >= kMaxBuildIdSize * 2 + 1);
  FTL_DCHECK(VerifyElfHdr(hdr));

  elf_off_t phoff = hdr->e_phoff;
  elf_half_t phnum = hdr->e_phnum;

  for (unsigned n = 0; n < phnum; n++) {
    mx_vaddr_t phaddr = vaddr + phoff + (n * sizeof(elf_phdr_t));
    elf_word_t type;
    if (!m.Read(phaddr + phdr_off_type, &type, sizeof(type)))
      return false;
    if (type != PT_NOTE)
      continue;

    elf_off_t off;
    elf_native_word_t size;
    if (!m.Read(phaddr + phdr_off_offset, &off, sizeof(off)))
      return false;
    if (!m.Read(phaddr + phdr_off_filesz, &size, sizeof(size)))
      return false;

    struct {
      Elf32_Nhdr hdr;
      char name[sizeof("GNU")];
    } hdr;
    while (size > sizeof(hdr)) {
      if (!m.Read(vaddr + off, &hdr, sizeof(hdr)))
        return false;
      size_t header_size = sizeof(Elf32_Nhdr) + ((hdr.hdr.n_namesz + 3) & -4);
      size_t payload_size = (hdr.hdr.n_descsz + 3) & -4;
      off += header_size;
      size -= header_size;
      mx_vaddr_t payload_vaddr = vaddr + off;
      off += payload_size;
      size -= payload_size;
      if (hdr.hdr.n_type != NT_GNU_BUILD_ID ||
          hdr.hdr.n_namesz != sizeof("GNU") ||
          memcmp(hdr.name, "GNU", sizeof("GNU")) != 0) {
        continue;
      }
      if (hdr.hdr.n_descsz > kMaxBuildIdSize) {
        // TODO(dje): Revisit.
        snprintf(buf, buf_size, "build_id_too_large_%u", hdr.hdr.n_descsz);
      } else {
        uint8_t buildid[kMaxBuildIdSize];
        if (!m.Read(payload_vaddr, buildid, hdr.hdr.n_descsz))
          return false;
        for (uint32_t i = 0; i < hdr.hdr.n_descsz; ++i) {
          snprintf(&buf[i * 2], 3, "%02x", buildid[i]);
        }
      }
      return true;
    }
  }

  *buf = '\0';
  return true;
}

}  // namespace elf
}  // namespace debugserver
