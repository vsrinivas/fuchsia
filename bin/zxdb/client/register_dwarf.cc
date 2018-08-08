// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/register_dwarf.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

using debug_ipc::RegisterID;

debug_ipc::RegisterID GetDWARFRegisterID(debug_ipc::Arch arch,
                                         uint32_t dwarf_reg_id) {
  switch (arch) {
    case debug_ipc::Arch::kX64:
      return GetX64DWARFRegisterID(dwarf_reg_id);
    case debug_ipc::Arch::kArm64:
      return GetARMv8DWARFRegisterID(dwarf_reg_id);
    case debug_ipc::Arch::kUnknown:
      FXL_NOTREACHED() << "Architecture should be known for DWARF mapping.";
      return debug_ipc::RegisterID::kUnknown;
  }
}

RegisterID GetX64DWARFRegisterID(uint32_t dwarf_reg_id) {
  // https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
  // Page 62
  switch (dwarf_reg_id) {
    case 0:
      return RegisterID::kX64_rax;
    case 1:
      return RegisterID::kX64_rdx;
    case 2:
      return RegisterID::kX64_rcx;
    case 3:
      return RegisterID::kX64_rbx;
    case 4:
      return RegisterID::kX64_rsi;
    case 5:
      return RegisterID::kX64_rdi;
    case 6:
      return RegisterID::kX64_rbp;
    case 7:
      return RegisterID::kX64_rsp;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15: {
      // 8 = r8, 15 = r15
      auto base = static_cast<uint32_t>(RegisterID::kX64_r8);
      return static_cast<RegisterID>(base - 8 + dwarf_reg_id);
    }
    // TODO(donosoc): 17-24 -> %xmm0 - %xmm7
    // TODO(donosoc): 25-32 -> %xmm8 - %xmm15
    // TODO(donosoc): 33-40 -> %st0 - %st7
    // TODO(donosoc): 41-48 -> %mm0 - %mm7
    case 49:
      return RegisterID::kX64_rflags;
      // TODO(donosoc): 50-55 -> (%es, %cs, %ss, %ds, %fs, %gs)
      // 56-57: Reserved
      // TODO(donsooc): 58 -> FS Base Address
      // TODO(donosoc): 59 -> GS Base Address
      // 60-61: Reserved
      // TODO(donosoc): 62 -> %ts (Task Register)
      // TODO(donosoc): 63 -> %ldtr
      // TODO(donosoc): 64 -> %mxcsr (128-bit Media Control and Status)
      // TODO(donosoc): 65 -> %fcw (x87 Control Word)
      // TODO(donosoc): 66 -> %fsw (x87 Status Word)
      // TODO(donosoc): 67-82 -> %xmm16–%xmm31 (Upper Vector Registers 16–31)
      // 83-117: Reserved
      // TODO(donosoc):118-125 -> %k0–%k7 (Vector Mask Registers 0–7)
      // TODO(donosoc):126-129 -> %bnd0–%bnd3 (Bound Registers 0–3)
  }

  return RegisterID::kUnknown;
}

debug_ipc::RegisterID GetARMv8DWARFRegisterID(uint32_t dwarf_reg_id) {
  // http://infocenter.arm.com/help/topic/com.arm.doc.ecm0665627/abi_sve_aadwarf_100985_0000_00_en.pdf
  // Page 6
  if (dwarf_reg_id <= 29) {
    auto base = static_cast<uint32_t>(RegisterID::kARMv8_x0);
    return static_cast<RegisterID>(base + dwarf_reg_id);
  }

  switch (dwarf_reg_id) {
    case 32:
      return RegisterID::kARMv8_sp;
      // 31: Reserved
      // TODO(donosoc): 33 -> ELR_mode
      // 34-45: Reserved
      // TODO(donosoc): 46 -> VG 64-bit SVE Vector granule pseudo register
      // TODO(donosoc): 47 -> FFR VG´8-bit SVE first fault register
      // TODO(donosoc): 48-63 -> P0-P15 VG´8-bit SVE predicate registers
      // TODO(donosoc): 64-95 -> V0-V31 128-bit FP/Advanced SIMD registers
      // TODO(donosoc): 96-127 -> Z0-Z31 VG´64-bit SVE vector registers
  }

  return RegisterID::kUnknown;
}

}  // namespace zxdb
