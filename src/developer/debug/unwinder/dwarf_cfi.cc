// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/dwarf_cfi.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <map>
#include <string>

#include "src/developer/debug/unwinder/dwarf_cfi_parser.h"
#include "third_party/crashpad/third_party/glibc/elf/elf.h"

namespace unwinder {

namespace {

// DWARF Common Information Entry.
struct DwarfCie {
  uint64_t code_alignment_factor = 0;       // usually 1
  int64_t data_alignment_factor = 0;        // usually -4 on arm64, -8 on x64
  RegisterID return_address_register;       // PC on x64, LR on arm64.
  bool fde_have_augmentation_data = false;  // should always be true
  uint8_t fde_address_encoding = 0xFF;      // invalid encoding
  uint64_t instructions_begin = 0;
  uint64_t instructions_end = 0;  // exclusive
};

// DWARF Frame Description Entry.
struct DwarfFde {
  uint64_t pc_begin = 0;
  uint64_t pc_end = 0;
  uint64_t instructions_begin = 0;
  uint64_t instructions_end = 0;  // exclusive
};

// Check and return the size of each entry in the table. It's doubled because each entry contains
// 2 addresses, i.e., the start_pc and the fde_offset.
[[nodiscard]] Error DecodeTableEntrySize(uint8_t table_enc, uint64_t& res) {
  if (table_enc == 0xFF) {  // DW_EH_PE_omit
    return Error("no binary search table");
  }
  if ((table_enc & 0xF0) != 0x30) {
    return Error("invalid table_enc");
  }
  switch (table_enc & 0x0F) {
    case 0x02:  // DW_EH_PE_udata2  A 2 bytes unsigned value.
    case 0x0A:  // DW_EH_PE_sdata2  A 2 bytes signed value.
      res = 4;
      return Success();
    case 0x03:  // DW_EH_PE_udata4  A 4 bytes unsigned value.
    case 0x0B:  // DW_EH_PE_sdata4  A 4 bytes signed value.
      res = 8;
      return Success();
    case 0x04:  // DW_EH_PE_udata8  An 8 bytes unsigned value.
    case 0x0C:  // DW_EH_PE_sdata8  An 8 bytes signed value.
      res = 16;
      return Success();
    default:
      return Error("unsupported table_enc: %#x", table_enc);
  }
}

// Decode length in CIE/FDE.
[[nodiscard]] Error DecodeLength(Memory* memory, uint64_t& ptr, uint64_t& length) {
  uint32_t short_length;
  if (auto err = memory->Read(ptr, short_length); err.has_err()) {
    return err;
  }
  if (short_length == 0) {
    return Error("not a valid CIE/FDE");
  }
  if (short_length == 0xFFFFFFFF) {
    if (auto err = memory->Read(ptr, length); err.has_err()) {
      return err;
    }
  } else {
    length = short_length;
  }
  return Success();
}

// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
[[nodiscard]] Error DecodeCIE(Memory* memory, uint64_t cie_ptr, DwarfCie& cie) {
  uint64_t length;
  if (auto err = DecodeLength(memory, cie_ptr, length); err.has_err()) {
    return err;
  }
  cie.instructions_end = cie_ptr + length;

  uint32_t cie_id;
  if (auto err = memory->Read(cie_ptr, cie_id); err.has_err()) {
    return err;
  }
  if (cie_id) {
    return Error("not a valid CIE");
  }

  // CIE version in .eh_frame should always be 1.
  uint8_t version;
  if (auto err = memory->Read(cie_ptr, version); err.has_err()) {
    return err;
  }
  if (version != 1) {
    return Error("unsupported CIE version: %d", version);
  }

  std::string augmentation_string;
  while (true) {
    char ch;
    if (auto err = memory->Read(cie_ptr, ch); err.has_err()) {
      return err;
    }
    if (ch) {
      augmentation_string.push_back(ch);
    } else {
      break;
    }
  }

  if (auto err = memory->ReadULEB128(cie_ptr, cie.code_alignment_factor); err.has_err()) {
    return err;
  }
  if (auto err = memory->ReadSLEB128(cie_ptr, cie.data_alignment_factor); err.has_err()) {
    return err;
  }
  if (auto err = memory->Read(cie_ptr, cie.return_address_register); err.has_err()) {
    return err;
  }

  if (augmentation_string.empty()) {
    cie.instructions_begin = cie_ptr;
    cie.fde_have_augmentation_data = false;
  } else {
    if (augmentation_string[0] != 'z') {
      return Error("invalid augmentation string: %s", augmentation_string.c_str());
    }
    uint64_t augmentation_length;
    if (auto err = memory->ReadULEB128(cie_ptr, augmentation_length); err.has_err()) {
      return err;
    }
    cie.instructions_begin = cie_ptr + augmentation_length;
    cie.fde_have_augmentation_data = true;

    for (char ch : augmentation_string) {
      switch (ch) {
        case 'L':  // not used now
          uint8_t lsda_encoding;
          if (auto err = memory->Read(cie_ptr, lsda_encoding); err.has_err()) {
            return err;
          }
          break;
        case 'P':  // not used now
          uint8_t enc;
          if (auto err = memory->Read(cie_ptr, enc); err.has_err()) {
            return err;
          }
          uint64_t personality;
          if (auto err = memory->ReadEncoded(cie_ptr, personality, enc, 0); err.has_err()) {
            return err;
          }
          break;
        case 'R':
          if (auto err = memory->Read(cie_ptr, cie.fde_address_encoding); err.has_err()) {
            return err;
          }
          break;
      }
    }
  }

  return Success();
}

// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
[[nodiscard]] Error DecodeFDE(Memory* memory, uint64_t fde_ptr, DwarfFde& fde, DwarfCie& cie) {
  uint64_t length;
  if (auto err = DecodeLength(memory, fde_ptr, length); err.has_err()) {
    return err;
  }
  fde.instructions_end = fde_ptr + length;

  uint32_t cie_offset;
  if (auto err = memory->Read(fde_ptr, cie_offset); err.has_err()) {
    return err;
  }
  if (auto err = DecodeCIE(memory, fde_ptr - 4 - cie_offset, cie); err.has_err()) {
    return err;
  }

  if (auto err = memory->ReadEncoded(fde_ptr, fde.pc_begin, cie.fde_address_encoding);
      err.has_err()) {
    return err;
  }
  if (auto err = memory->ReadEncoded(fde_ptr, fde.pc_end, cie.fde_address_encoding & 0x0F);
      err.has_err()) {
    return err;
  }
  fde.pc_end += fde.pc_begin;

  if (cie.fde_have_augmentation_data) {
    uint64_t augmentation_length;
    if (auto err = memory->ReadULEB128(fde_ptr, augmentation_length); err.has_err()) {
      return err;
    }
    // We don't really care about the augmentation data.
    fde_ptr += augmentation_length;
  }
  fde.instructions_begin = fde_ptr;

  return Success();
}

}  // namespace

// Load the .eh_frame from the ELF image in memory.
//
// See the Linux Standard Base Core Specification
// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
// and a reference implementation in LLVM
// https://github.com/llvm/llvm-project/blob/main/libunwind/src/DwarfParser.hpp
// https://github.com/llvm/llvm-project/blob/main/libunwind/src/EHHeaderParser.hpp
Error DwarfCfi::Load(Memory* elf, uint64_t elf_ptr) {
  Elf64_Ehdr ehdr;
  if (auto err = elf->Read(+elf_ptr, ehdr); err.has_err()) {
    return err;
  }

  // Header magic should be correct.
  if (strncmp(reinterpret_cast<const char*>(ehdr.e_ident), ELFMAG, SELFMAG) != 0) {
    return Error("not an ELF image");
  }

  // Finds the .eh_frame_hdr section.
  eh_frame_hdr_ptr_ = 0;
  pc_begin_ = -1;
  pc_end_ = 0;
  for (uint64_t i = 0; i < ehdr.e_phnum; i++) {
    Elf64_Phdr phdr;
    if (auto err = elf->Read(elf_ptr + ehdr.e_phoff + ehdr.e_phentsize * i, phdr); err.has_err()) {
      return err;
    }
    if (phdr.p_type == PT_GNU_EH_FRAME) {
      eh_frame_hdr_ptr_ = elf_ptr + phdr.p_vaddr;
    } else if (phdr.p_type == PT_LOAD && phdr.p_flags & PF_X) {
      pc_begin_ = std::min(pc_begin_, elf_ptr + phdr.p_vaddr);
      pc_end_ = std::max(pc_end_, elf_ptr + phdr.p_vaddr + phdr.p_memsz);
    }
  }
  if (!eh_frame_hdr_ptr_) {
    return Error("no PT_GNU_EH_FRAME segment");
  }

  auto p = eh_frame_hdr_ptr_;
  uint8_t version;
  if (auto err = elf->Read(p, version); err.has_err()) {
    return err;
  }
  if (version != 1) {
    return Error("unknown eh_frame_hdr version %d", version);
  }

  uint8_t eh_frame_ptr_enc;
  uint8_t fde_count_enc;
  uint64_t eh_frame_ptr;  // not used
  if (auto err = elf->Read<uint8_t>(p, eh_frame_ptr_enc); err.has_err()) {
    return err;
  }
  if (auto err = elf->Read<uint8_t>(p, fde_count_enc); err.has_err()) {
    return err;
  }
  if (auto err = elf->Read<uint8_t>(p, table_enc_); err.has_err()) {
    return err;
  }
  if (auto err = DecodeTableEntrySize(table_enc_, table_entry_size_); err.has_err()) {
    return err;
  }
  if (auto err = elf->ReadEncoded(p, eh_frame_ptr, eh_frame_ptr_enc, eh_frame_hdr_ptr_);
      err.has_err()) {
    return err;
  }
  if (auto err = elf->ReadEncoded(p, fde_count_, fde_count_enc, eh_frame_hdr_ptr_); err.has_err()) {
    return err;
  }
  table_ptr_ = p;

  if (fde_count_ == 0) {
    return Error("empty binary search table");
  }

  elf_ = elf;
  return Success();
}

Error DwarfCfi::Step(Memory* stack, const Registers& current, Registers& next) {
  uint64_t pc;
  if (auto err = current.GetPC(pc); err.has_err()) {
    return err;
  }
  if (pc < pc_begin_ || pc >= pc_end_) {
    return Error("pc %#" PRIx64 " is outside of the executable area", pc);
  }

  // Binary search for fde_ptr in the range [low, high).
  uint64_t low = 0;
  uint64_t high = fde_count_;
  while (low + 1 < high) {
    uint64_t mid = (low + high) / 2;
    uint64_t addr = table_ptr_ + mid * table_entry_size_;
    uint64_t mid_pc;
    if (auto err = elf_->ReadEncoded(addr, mid_pc, table_enc_, eh_frame_hdr_ptr_); err.has_err()) {
      return err;
    }
    if (pc < mid_pc) {
      high = mid;
    } else {
      low = mid;
    }
  }
  uint64_t addr = table_ptr_ + low * table_entry_size_ + table_entry_size_ / 2;
  uint64_t fde_ptr;
  if (auto err = elf_->ReadEncoded(addr, fde_ptr, table_enc_, eh_frame_hdr_ptr_); err.has_err()) {
    return err;
  }

  DwarfCie cie;
  DwarfFde fde;
  if (auto err = DecodeFDE(elf_, fde_ptr, fde, cie); err.has_err()) {
    return err;
  }
  if (pc < fde.pc_begin || pc >= fde.pc_end) {
    return Error("cannot find FDE for pc %#" PRIx64, pc);
  }

  DwarfCfiParser cfi_parser(current.arch());

  // Parse instructions in CIE first.
  if (auto err =
          cfi_parser.ParseInstructions(elf_, cie.code_alignment_factor, cie.data_alignment_factor,
                                       cie.instructions_begin, cie.instructions_end, -1);
      err.has_err()) {
    return err;
  }

  cfi_parser.Snapshot();

  // Parse instructions in FDE until pc.
  if (auto err = cfi_parser.ParseInstructions(elf_, cie.code_alignment_factor,
                                              cie.data_alignment_factor, fde.instructions_begin,
                                              fde.instructions_end, pc - fde.pc_begin);
      err.has_err()) {
    return err;
  }

  if (auto err = cfi_parser.Step(stack, cie.return_address_register, current, next);
      err.has_err()) {
    return err;
  }

  return Success();
}

}  // namespace unwinder
