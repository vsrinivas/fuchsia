/*
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "elf.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "garnet/lib/debugger_utils/elf_reader.h"
#include "garnet/lib/debugger_utils/byte_block_file.h"
#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/logging.h"

#include "third_party/processor-trace/libipt/include/intel-pt.h"

namespace simple_pt {

// Read in the symbol table(s) of |elf|.
// |base| is the address at which the file was loaded, and |len|
// is the length of the text segment.
// |offset| is the different between where the file was actually
// loaded (|base|) and address recorded in the file.

static bool ReadSymtabs(debugger_utils::ElfReader* elf,
                        uint64_t cr3,
                        uint64_t base,
                        uint64_t len,
                        uint64_t offset,
                        bool is_kernel,
                        std::unique_ptr<SymbolTable>* out_symtab,
                        std::unique_ptr<SymbolTable>* out_dynsym) {
  size_t num_sections = elf->GetNumSections();

  debugger_utils::ElfError rc = elf->ReadSectionHeaders();
  if (rc != debugger_utils::ElfError::OK) {
    FXL_LOG(ERROR) << "Error reading ELF section headers: "
                   << ElfErrorName(rc);
    return false;
  }

  std::unique_ptr<SymbolTable> symtab;
  std::unique_ptr<SymbolTable> dynsym;

  for (size_t i = 0; i < num_sections; ++i) {
    const debugger_utils::ElfSectionHeader& shdr = elf->GetSectionHeader(i);
    SymbolTable* st;
    switch (shdr.sh_type) {
      case SHT_SYMTAB:
        symtab.reset(
            new SymbolTable(elf, "symtab", cr3, base, offset, is_kernel));
        st = symtab.get();
        break;
      case SHT_DYNSYM:
        dynsym.reset(
            new SymbolTable(elf, "dynsym", cr3, base, offset, is_kernel));
        st = dynsym.get();
        break;
      default:
        continue;
    }

    if (!st->Populate(elf, shdr.sh_type))
      return false;

    // Compute the last address used by symbols in the table.
    size_t num_symbols = st->num_symbols();
    uint64_t end = 0;
    for (size_t j = 0; j < num_symbols; j++) {
      const debugger_utils::ElfSymbol& sym = st->GetSymbol(j);
      if (end < sym.addr + sym.size)
        end = sym.addr + sym.size;
    }

    // Assign the full range of symbols to the symtab so that even if a symbol
    // isn't found, we still know the pc came from this file.
    // Other segments technically needn't be contiguous, which one would have
    // to deal with to handle more than just the (assumed) one text segment.
    if (len > end)
      st->set_end(offset + len);
    else
      st->set_end(offset + end);
  }

  if (!symtab && !dynsym)
    FXL_LOG(WARNING) << elf->file_name() << " has no symbols";
  *out_symtab = std::move(symtab);
  *out_dynsym = std::move(dynsym);

  return true;
}

// Find the base address, length, and file offset of the text segment
// of a non-PIC ELF.

static void FindBaseLenFileoff(debugger_utils::ElfReader* elf,
                               uint64_t* base,
                               uint64_t* len,
                               uint64_t* fileoff) {
  size_t num_segments = elf->GetNumSegments();
  for (size_t i = 0; i < num_segments; ++i) {
    const debugger_utils::ElfSegmentHeader& phdr = elf->GetSegmentHeader(i);
    if (phdr.p_type == PT_LOAD && (phdr.p_flags & PF_X) != 0) {
      *base = phdr.p_vaddr;
      *len = phdr.p_memsz;
      *fileoff = phdr.p_offset;
      return;
    }
  }
}

// Given a potential PIC ELF loaded at |base|, compute the offset from where
// the file says segments are loaded to where they were actually loaded.

static void FindOffset(debugger_utils::ElfReader* elf, uint64_t base, uint64_t* offset) {
  uint64_t minaddr = UINT64_MAX;

  if (!base) {
    *offset = 0;
    return;
  }

  size_t num_segments = elf->GetNumSegments();
  for (size_t i = 0; i < num_segments; ++i) {
    const debugger_utils::ElfSegmentHeader& phdr = elf->GetSegmentHeader(i);
    if (phdr.p_type == PT_LOAD && phdr.p_vaddr < minaddr) {
      minaddr = phdr.p_vaddr;
    }
  }

  // Punt if no loadable segments found.
  if (minaddr == UINT64_MAX) {
    *offset = 0;
    return;
  }

  *offset = base - minaddr;
}

static void AddProgbits(debugger_utils::ElfReader* elf,
                        struct pt_image* image,
                        const char* file_name,
                        uint64_t base,
                        uint64_t cr3,
                        uint64_t offset,
                        uint64_t file_off,
                        uint64_t map_len) {
  size_t num_segments = elf->GetNumSegments();
  for (size_t i = 0; i < num_segments; ++i) {
    const debugger_utils::ElfSegmentHeader& phdr = elf->GetSegmentHeader(i);

    if ((phdr.p_type == PT_LOAD) && (phdr.p_flags & PF_X) &&
        phdr.p_offset >= file_off &&
        (!map_len || phdr.p_offset + phdr.p_filesz <= file_off + map_len)) {
      struct pt_asid asid;
      int err;

      /* The first loadable section in zircon.elf is
         unusable to us. Plus we want to ignore it here.
         This test is an attempt to not be too zircon specific. */
      if (phdr.p_vaddr < phdr.p_paddr)
        continue;

      pt_asid_init(&asid);
      asid.cr3 = cr3;
      errno = 0;

      err = pt_image_add_file(image, file_name, phdr.p_offset, phdr.p_filesz,
                              &asid, phdr.p_vaddr + offset);
      /* Duplicate. Just ignore. */
      if (err == -pte_bad_image)
        continue;
      if (err < 0) {
        fprintf(stderr, "reading prog code at %" PRIx64 ":%" PRIx64 " from %s: %s (%s): %d\n",
                phdr.p_vaddr, phdr.p_filesz, file_name,
                pt_errstr(pt_errcode(err)), errno ? strerror(errno) : "", err);
        return;
      }
    }
  }
}

static bool ElfOpen(const char* file_name,
                    std::unique_ptr<debugger_utils::ElfReader>* out_elf) {
  int fd = open(file_name, O_RDONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << file_name << ", " << debugger_utils::ErrnoString(errno);
    return false;
  }

  auto bb = std::shared_ptr<debugger_utils::FileByteBlock>(new debugger_utils::FileByteBlock(fd));

  std::unique_ptr<debugger_utils::ElfReader> elf;
  debugger_utils::ElfError rc = debugger_utils::ElfReader::Create(file_name, bb, 0, 0, &elf);
  if (rc != debugger_utils::ElfError::OK) {
    FXL_LOG(ERROR) << "Error creating ELF reader: " << debugger_utils::ElfErrorName(rc);
    return false;
  }

  rc = elf->ReadSegmentHeaders();
  if (rc != debugger_utils::ElfError::OK) {
    FXL_LOG(ERROR) << "Error reading ELF segment headers: "
                   << ElfErrorName(rc);
    return false;
  }

  *out_elf = std::move(elf);
  return true;
}

bool ReadElf(const char* file_name, struct pt_image* image,
             uint64_t base, uint64_t cr3,
             uint64_t file_off, uint64_t map_len,
             std::unique_ptr<SymbolTable>* out_symtab,
             std::unique_ptr<SymbolTable>* out_dynsym) {
  std::unique_ptr<debugger_utils::ElfReader> elf;
  if (!ElfOpen(file_name, &elf))
    return false;

  bool pic = false;
  const debugger_utils::ElfHeader& hdr = elf->header();
  pic = hdr.e_type == ET_DYN;
  if (pic && base == 0) {
    FXL_LOG(ERROR) << "PIC/PIE ELF with base 0 is not supported";
    return false;
  }

  uint64_t offset = 0;
  if (pic)
    FindOffset(elf.get(), base, &offset);

  if (!ReadSymtabs(elf.get(), cr3, base, map_len, offset, false,
                   out_symtab, out_dynsym))
    return false;

  AddProgbits(elf.get(), image, file_name, base, cr3, offset, file_off,
              map_len);

  return true;
}

bool ReadNonPicElf(const char* file_name, pt_image* image,
                   uint64_t cr3, bool is_kernel,
                   std::unique_ptr<SymbolTable>* out_symtab,
                   std::unique_ptr<SymbolTable>* out_dynsym) {
  std::unique_ptr<debugger_utils::ElfReader> elf;
  if (!ElfOpen(file_name, &elf))
    return false;

  // TODO(dje): kernel pc values can appear in traces with userspace cr3
  // values, e.g., when performing a syscall. For now, ignore cr3 for
  // kernel pcs. The original value of zero was odd anyway.
  uint64_t kernel_cr3_for_symtab = pt_asid_no_cr3;

  // TODO(dje): Need to handle PIE kernels.
  uint64_t base = 0, len = 0;
  uint64_t offset = 0, file_off = 0;
  FindBaseLenFileoff(elf.get(), &base, &len, &file_off);

  if (!ReadSymtabs(elf.get(), kernel_cr3_for_symtab, base, len, offset,
                   is_kernel, out_symtab, out_dynsym))
    return false;

  AddProgbits(elf.get(), image, file_name, base, cr3 ? cr3 : pt_asid_no_cr3,
              offset, file_off, len);

  return true;
}

}  // namespace simple_pt
