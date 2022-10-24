// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/dwarf_cfi.h"

#include <elf.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "src/developer/debug/unwinder/dwarf_cfi_parser.h"
#include "src/developer/debug/unwinder/error.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

namespace {

// The CIE ID that distinguishes a CIE from an FDE. They are only used in version 4.
// In version 1, the CIE ID is 0.
const constexpr uint32_t kDwarf32CieId = std::numeric_limits<uint32_t>::max();
const constexpr uint64_t kDwarf64CieId = std::numeric_limits<uint64_t>::max();

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

// Decode the length and cie_ptr field in CIE/FDE. It's awkward because we want to support both
// .eh_frame format and .debug_frame format.
[[nodiscard]] Error DecodeCieFdeHdr(Memory* elf, uint8_t version, uint64_t& ptr, uint64_t& end,
                                    uint64_t& cie_id) {
  uint32_t short_length;
  if (auto err = elf->Read(ptr, short_length); err.has_err()) {
    return err;
  }
  if (short_length == 0) {
    return Error("not a valid CIE/FDE");
  }
  if (short_length != 0xFFFFFFFF) {
    end = ptr + short_length;
  } else {
    uint64_t length;
    if (auto err = elf->Read(ptr, length); err.has_err()) {
      return err;
    }
    end = ptr + length;
  }
  // The cie_id is 8-bytes only when the version is 4 and it's a 64-bit DWARF format.
  if (version == 4 && short_length == 0xFFFFFFFF) {
    if (auto err = elf->Read(ptr, cie_id); err.has_err()) {
      return err;
    }
  } else {
    uint32_t short_cie_id;
    if (auto err = elf->Read(ptr, short_cie_id); err.has_err()) {
      return err;
    }
    // Special handling for cie_id in .debug_frame so that the callers don't need to distinguish
    // 32/64-bit DWARF to know whether an entry is CIE or FDE.
    if (version == 4 && short_cie_id == kDwarf32CieId) {
      cie_id = kDwarf64CieId;
    } else {
      cie_id = short_cie_id;
    }
  }
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
Error DwarfCfi::Load() {
  if (!elf_) {
    return Error("no elf memory");
  }

  Elf64_Ehdr ehdr;
  // Do not modify elf_ptr_.
  if (auto err = elf_->Read(+elf_ptr_, ehdr); err.has_err()) {
    return err;
  }

  // Header magic should be correct.
  if (strncmp(reinterpret_cast<const char*>(ehdr.e_ident), ELFMAG, SELFMAG) != 0) {
    return Error("not an ELF image");
  }

  // ==============================================================================================
  // Load from the .eh_frame_hdr section.
  // ==============================================================================================
  eh_frame_hdr_ptr_ = 0;
  pc_begin_ = -1;
  pc_end_ = 0;
  for (uint64_t i = 0; i < ehdr.e_phnum; i++) {
    Elf64_Phdr phdr;
    if (auto err = elf_->Read(elf_ptr_ + ehdr.e_phoff + ehdr.e_phentsize * i, phdr);
        err.has_err()) {
      return err;
    }
    if (phdr.p_type == PT_GNU_EH_FRAME) {
      eh_frame_hdr_ptr_ = elf_ptr_ + phdr.p_vaddr;
    } else if (phdr.p_type == PT_LOAD && phdr.p_flags & PF_X) {
      pc_begin_ = std::min(pc_begin_, elf_ptr_ + phdr.p_vaddr);
      pc_end_ = std::max(pc_end_, elf_ptr_ + phdr.p_vaddr + phdr.p_memsz);
    }
  }
  if (!eh_frame_hdr_ptr_) {
    return Error("no PT_GNU_EH_FRAME segment");
  }

  auto p = eh_frame_hdr_ptr_;
  uint8_t version;
  if (auto err = elf_->Read(p, version); err.has_err()) {
    return err;
  }
  if (version != 1) {
    return Error("unknown eh_frame_hdr version %d", version);
  }

  uint8_t eh_frame_ptr_enc;
  uint8_t fde_count_enc;
  uint64_t eh_frame_ptr;  // not used
  if (auto err = elf_->Read<uint8_t>(p, eh_frame_ptr_enc); err.has_err()) {
    return err;
  }
  if (auto err = elf_->Read<uint8_t>(p, fde_count_enc); err.has_err()) {
    return err;
  }
  if (auto err = elf_->Read<uint8_t>(p, table_enc_); err.has_err()) {
    return err;
  }
  if (auto err = DecodeTableEntrySize(table_enc_, table_entry_size_); err.has_err()) {
    return err;
  }
  if (auto err = elf_->ReadEncoded(p, eh_frame_ptr, eh_frame_ptr_enc, eh_frame_hdr_ptr_);
      err.has_err()) {
    return err;
  }
  if (auto err = elf_->ReadEncoded(p, fde_count_, fde_count_enc, eh_frame_hdr_ptr_);
      err.has_err()) {
    return err;
  }
  table_ptr_ = p;

  if (fde_count_ == 0) {
    return Error("empty binary search table");
  }

  // ==============================================================================================
  // Optionally load from the .debug_frame section. Any failure is not an error here.
  // ==============================================================================================
  debug_frame_ptr_ = 0;
  debug_frame_end_ = 0;
  // if ehdr.e_shstrndx is 0, it means there's no section info, i.e., the binary is stripped.
  if (!ehdr.e_shstrndx) {
    return Success();
  }
  uint64_t shstr_hdr_ptr =
      elf_ptr_ + ehdr.e_shoff + static_cast<uint64_t>(ehdr.e_shentsize) * ehdr.e_shstrndx;
  Elf64_Shdr shstr_hdr;
  // Even when the binary is not stripped, the .shstrtab and .debug_frame sections are by default
  // not loaded.
  if (elf_->Read(shstr_hdr_ptr, shstr_hdr).has_err()) {
    return Success();
  }
  for (uint64_t i = 0; i < ehdr.e_shnum; i++) {
    Elf64_Shdr shdr;
    if (elf_->Read(elf_ptr_ + ehdr.e_shoff + ehdr.e_shentsize * i, shdr).has_err()) {
      continue;
    }
    static constexpr char target_section_name[] = ".debug_frame";
    char section_name[sizeof(target_section_name)];
    if (elf_->Read(elf_ptr_ + shstr_hdr.sh_offset + shdr.sh_name, section_name).has_err()) {
      continue;
    }
    if (strncmp(section_name, target_section_name, sizeof(section_name)) == 0) {
      debug_frame_ptr_ = elf_ptr_ + shdr.sh_offset;
      debug_frame_end_ = debug_frame_ptr_ + shdr.sh_size;
    }
  }
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

  DwarfCie cie;
  DwarfFde fde;
  // Search for .eh_frame first.
  if (auto err = SearchEhFrame(pc, cie, fde); err.has_err()) {
    // Cannot find the correct FDE in .eh_frame, try to find in .debug_frame.
    if (SearchDebugFrame(pc, cie, fde).has_err()) {
      // Heuristic for PLT trampolines.
      if (!StepPLT(stack, current, next).has_err()) {
        return Success();
      }
      return err;  // return the error from .eh_frame.
    }
  }

  DwarfCfiParser cfi_parser(current.arch(), cie.code_alignment_factor, cie.data_alignment_factor);

  // Parse instructions in CIE first.
  if (auto err =
          cfi_parser.ParseInstructions(elf_, cie.instructions_begin, cie.instructions_end, -1);
      err.has_err()) {
    return err;
  }

  cfi_parser.Snapshot();

  // Parse instructions in FDE until pc.
  if (auto err = cfi_parser.ParseInstructions(elf_, fde.instructions_begin, fde.instructions_end,
                                              pc - fde.pc_begin);
      err.has_err()) {
    return err;
  }

  if (auto err = cfi_parser.Step(stack, cie.return_address_register, current, next);
      err.has_err()) {
    return err;
  }

  return Success();
}

Error DwarfCfi::SearchEhFrame(uint64_t pc, DwarfCie& cie, DwarfFde& fde) {
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

  if (auto err = DecodeFde(1, fde_ptr, cie, fde); err.has_err()) {
    return err;
  }
  if (pc < fde.pc_begin || pc >= fde.pc_end) {
    return Error("cannot find FDE for pc %#" PRIx64, pc);
  }
  return Success();
}

Error DwarfCfi::SearchDebugFrame(uint64_t pc, DwarfCie& cie, DwarfFde& fde) {
  if (!debug_frame_ptr_) {
    return Error("no .debug_frame section");
  }
  if (debug_frame_map_.empty()) {
    if (auto err = BuildDebugFrameMap(); err.has_err()) {
      return err;
    }
  }

  auto debug_frame_map_it = debug_frame_map_.upper_bound(pc);
  if (debug_frame_map_it == debug_frame_map_.begin()) {
    return Error("cannot find FDE for pc %#" PRIx64 " in .debug_frame", pc);
  }
  debug_frame_map_it--;
  uint64_t fde_ptr = debug_frame_map_it->second;

  if (auto err = DecodeFde(4, fde_ptr, cie, fde); err.has_err()) {
    return err;
  }
  if (pc < fde.pc_begin || pc >= fde.pc_end) {
    return Error("cannot find FDE for pc %#" PRIx64 " in .debug_frame", pc);
  }
  return Success();
}

// In order to read less memory, this function assumes the address_size of all CIEs is the same,
// so that it only needs to decode the first CIE.
Error DwarfCfi::BuildDebugFrameMap() {
  debug_frame_map_.clear();
  uint8_t fde_address_encoding = 0;
  for (uint64_t p = debug_frame_ptr_, next_p; p < debug_frame_end_; p = next_p) {
    uint64_t this_p = p;
    uint64_t cie_id;
    if (auto err = DecodeCieFdeHdr(elf_, 4, p, next_p, cie_id); err.has_err()) {
      return err;
    }
    if (cie_id == kDwarf64CieId) {
      if (fde_address_encoding) {
        // Assume address_size is the same for all CIEs. Skip all other CIEs to accelerate.
        continue;
      }
      DwarfCie cie;
      if (auto err = DecodeCie(4, this_p, cie); err.has_err()) {
        return err;
      }
      fde_address_encoding = cie.fde_address_encoding;
    } else {  // is FDE
      uint64_t pc_begin;
      if (auto err = elf_->ReadEncoded(p, pc_begin, fde_address_encoding, elf_ptr_);
          err.has_err()) {
        return err;
      }
      debug_frame_map_.emplace(pc_begin, this_p);
    }
  }
  if (debug_frame_map_.empty()) {
    return Error("empty .debug_frame");
  }
  return Success();
}

// When version == 1, check the spec at
// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
// When version == 4, check the spec at http://www.dwarfstd.org/doc/DWARF5.pdf
Error DwarfCfi::DecodeCie(uint8_t version, uint64_t cie_ptr, DwarfCie& cie) {
  uint64_t cie_id;
  if (auto err = DecodeCieFdeHdr(elf_, version, cie_ptr, cie.instructions_end, cie_id);
      err.has_err()) {
    return err;
  }
  if ((version == 1 && cie_id != 0) || (version == 4 && cie_id != kDwarf64CieId)) {
    return Error("not a valid CIE");
  }

  // Versions should match.
  uint8_t this_version;
  if (auto err = elf_->Read(cie_ptr, this_version); err.has_err()) {
    return err;
  }
  if (this_version != version) {
    return Error("unexpected CIE version: %d", this_version);
  }

  std::string augmentation_string;
  while (true) {
    char ch;
    if (auto err = elf_->Read(cie_ptr, ch); err.has_err()) {
      return err;
    }
    if (ch) {
      augmentation_string.push_back(ch);
    } else {
      break;
    }
  }

  if (version == 4) {
    // Read the address_size.
    uint8_t address_size;
    if (auto err = elf_->Read(cie_ptr, address_size); err.has_err()) {
      return err;
    }
    // Set fde_address_encoding to DW_EH_PE_datarel so that we can set the base to elf_ptr_.
    switch (address_size) {
      case 2:
        cie.fde_address_encoding = 0x32;
        break;
      case 4:
        cie.fde_address_encoding = 0x33;
        break;
      case 8:
        cie.fde_address_encoding = 0x34;
        break;
      default:
        return Error("unsupported CIE address_size: %d", address_size);
    }
    // Skip the segment_selector_size.
    cie_ptr++;
  }

  if (auto err = elf_->ReadULEB128(cie_ptr, cie.code_alignment_factor); err.has_err()) {
    return err;
  }
  if (auto err = elf_->ReadSLEB128(cie_ptr, cie.data_alignment_factor); err.has_err()) {
    return err;
  }
  if (version == 4) {
    uint64_t return_address_register;
    if (auto err = elf_->ReadULEB128(cie_ptr, return_address_register); err.has_err()) {
      return err;
    }
    cie.return_address_register = static_cast<RegisterID>(return_address_register);
  } else {
    if (auto err = elf_->Read(cie_ptr, cie.return_address_register); err.has_err()) {
      return err;
    }
  }

  if (augmentation_string.empty()) {
    cie.instructions_begin = cie_ptr;
    cie.fde_have_augmentation_data = false;
  } else {
    // DWARF standard doesn't say anything about the possibility of the augmentation string and
    // we have never seen a use case of augmentation string in .debug_frame, which is understandable
    // because the augmentation string is mainly useful for unwinding during an exception.
    // For now, we don't support it.
    if (version == 4) {
      return Error("unsupported augmentation string in .debug_frame: %s",
                   augmentation_string.c_str());
    }
    if (augmentation_string[0] != 'z') {
      return Error("invalid augmentation string: %s", augmentation_string.c_str());
    }
    uint64_t augmentation_length;
    if (auto err = elf_->ReadULEB128(cie_ptr, augmentation_length); err.has_err()) {
      return err;
    }
    cie.instructions_begin = cie_ptr + augmentation_length;
    cie.fde_have_augmentation_data = true;

    for (char ch : augmentation_string) {
      switch (ch) {
        case 'L':
          // LSDA (language-specific data area) is used by some languages such as C++ to ensure
          // the correct destruction of objects on stack. We don't need to handle it.
          uint8_t lsda_encoding;
          if (auto err = elf_->Read(cie_ptr, lsda_encoding); err.has_err()) {
            return err;
          }
          break;
        case 'P':
          // The personality routine is used to handle language and vendor-specific tasks to ensure
          // the correct unwinding. We don't need to handle it.
          uint8_t enc;
          if (auto err = elf_->Read(cie_ptr, enc); err.has_err()) {
            return err;
          }
          uint64_t personality;
          if (auto err = elf_->ReadEncoded(cie_ptr, personality, enc, 0); err.has_err()) {
            return err;
          }
          break;
        case 'R':
          if (auto err = elf_->Read(cie_ptr, cie.fde_address_encoding); err.has_err()) {
            return err;
          }
          break;
      }
    }
  }

  return Success();
}

Error DwarfCfi::DecodeFde(uint8_t version, uint64_t fde_ptr, DwarfCie& cie, DwarfFde& fde) {
  uint64_t cie_offset;
  if (auto err = DecodeCieFdeHdr(elf_, version, fde_ptr, fde.instructions_end, cie_offset);
      err.has_err()) {
    return err;
  }

  uint64_t cie_ptr;
  if (version == 4) {
    cie_ptr = debug_frame_ptr_ + cie_offset;
  } else {
    cie_ptr = fde_ptr - 4 - cie_offset;
  }
  if (auto err = DecodeCie(version, cie_ptr, cie); err.has_err()) {
    return err;
  }

  if (auto err = elf_->ReadEncoded(fde_ptr, fde.pc_begin, cie.fde_address_encoding, elf_ptr_);
      err.has_err()) {
    return err;
  }
  if (auto err = elf_->ReadEncoded(fde_ptr, fde.pc_end, cie.fde_address_encoding & 0x0F);
      err.has_err()) {
    return err;
  }
  fde.pc_end += fde.pc_begin;

  if (cie.fde_have_augmentation_data) {
    uint64_t augmentation_length;
    if (auto err = elf_->ReadULEB128(fde_ptr, augmentation_length); err.has_err()) {
      return err;
    }
    // We don't really care about the augmentation data.
    fde_ptr += augmentation_length;
  }
  fde.instructions_begin = fde_ptr;

  return Success();
}

Error DwarfCfi::StepPLT(Memory* stack, const Registers& current, Registers& next) {
  if (current.arch() == Registers::Arch::kX64) {
    uint64_t sp;
    if (auto err = current.GetSP(sp); err.has_err()) {
      return err;
    }
    uint64_t sp_val;
    if (auto err = stack->Read(sp, sp_val); err.has_err()) {
      return err;
    }
    if (sp_val < pc_begin_ || sp_val >= pc_end_) {
      return Error("It doesn't look like a PLT trampoline");
    }
    // A trampoline will usually not scratch any registers, we could copy all the register values.
    next = current;
    // Simulate a return.
    next.SetPC(sp_val);
    next.SetSP(sp + 8);
    return Success();
  }
  if (current.arch() == Registers::Arch::kArm64) {
    uint64_t lr;
    if (auto err = current.Get(RegisterID::kArm64_lr, lr); err.has_err()) {
      return err;
    }
    if (lr < pc_begin_ || lr >= pc_end_) {
      return Error("It doesn't look like a PLT trampoline");
    }
    next = current;
    next.SetPC(lr);
    next.Unset(RegisterID::kArm64_lr);
    return Success();
  }
  return Error("Not supported yet");
}

}  // namespace unwinder
