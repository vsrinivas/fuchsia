// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/register_desc.h"

#include <algorithm>
#include <map>

#include "src/developer/debug/ipc/protocol.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {
namespace {

// clang-format off

// Canonical registers, these all have a 1:1 mapping between "id" and "name".
const RegisterInfo kRegisterInfo[] = {
    // ARMv8
    // ---------------------------------------------------------------------------------------------

    // General purpose.
    // NOTE: The DWARF ID for tpidr is not in any spec. mcgrathr@ invented it for our APIs, and it
    // may change as those get standardized.

    {.id = RegisterID::kARMv8_x0,  .name = "x0",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x0,  .bits = 64, .dwarf_id = 0},
    {.id = RegisterID::kARMv8_x1,  .name = "x1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x1,  .bits = 64, .dwarf_id = 1},
    {.id = RegisterID::kARMv8_x2,  .name = "x2",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x2,  .bits = 64, .dwarf_id = 2},
    {.id = RegisterID::kARMv8_x3,  .name = "x3",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x3,  .bits = 64, .dwarf_id = 3},
    {.id = RegisterID::kARMv8_x4,  .name = "x4",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x4,  .bits = 64, .dwarf_id = 4},
    {.id = RegisterID::kARMv8_x5,  .name = "x5",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x5,  .bits = 64, .dwarf_id = 5},
    {.id = RegisterID::kARMv8_x6,  .name = "x6",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x6,  .bits = 64, .dwarf_id = 6},
    {.id = RegisterID::kARMv8_x7,  .name = "x7",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x7,  .bits = 64, .dwarf_id = 7},
    {.id = RegisterID::kARMv8_x8,  .name = "x8",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x8,  .bits = 64, .dwarf_id = 8},
    {.id = RegisterID::kARMv8_x9,  .name = "x9",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x9,  .bits = 64, .dwarf_id = 9},
    {.id = RegisterID::kARMv8_x10, .name = "x10", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x10, .bits = 64, .dwarf_id = 10},
    {.id = RegisterID::kARMv8_x11, .name = "x11", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x11, .bits = 64, .dwarf_id = 11},
    {.id = RegisterID::kARMv8_x12, .name = "x12", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x12, .bits = 64, .dwarf_id = 12},
    {.id = RegisterID::kARMv8_x13, .name = "x13", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x13, .bits = 64, .dwarf_id = 13},
    {.id = RegisterID::kARMv8_x14, .name = "x14", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x14, .bits = 64, .dwarf_id = 14},
    {.id = RegisterID::kARMv8_x15, .name = "x15", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x15, .bits = 64, .dwarf_id = 15},
    {.id = RegisterID::kARMv8_x16, .name = "x16", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x16, .bits = 64, .dwarf_id = 16},
    {.id = RegisterID::kARMv8_x17, .name = "x17", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x17, .bits = 64, .dwarf_id = 17},
    {.id = RegisterID::kARMv8_x18, .name = "x18", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x18, .bits = 64, .dwarf_id = 18},
    {.id = RegisterID::kARMv8_x19, .name = "x19", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x19, .bits = 64, .dwarf_id = 19},
    {.id = RegisterID::kARMv8_x20, .name = "x20", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x20, .bits = 64, .dwarf_id = 20},
    {.id = RegisterID::kARMv8_x21, .name = "x21", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x21, .bits = 64, .dwarf_id = 21},
    {.id = RegisterID::kARMv8_x22, .name = "x22", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x22, .bits = 64, .dwarf_id = 22},
    {.id = RegisterID::kARMv8_x23, .name = "x23", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x23, .bits = 64, .dwarf_id = 23},
    {.id = RegisterID::kARMv8_x24, .name = "x24", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x24, .bits = 64, .dwarf_id = 24},
    {.id = RegisterID::kARMv8_x25, .name = "x25", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x25, .bits = 64, .dwarf_id = 25},
    {.id = RegisterID::kARMv8_x26, .name = "x26", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x26, .bits = 64, .dwarf_id = 26},
    {.id = RegisterID::kARMv8_x27, .name = "x27", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x27, .bits = 64, .dwarf_id = 27},
    {.id = RegisterID::kARMv8_x28, .name = "x28", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x28, .bits = 64, .dwarf_id = 28},
    {.id = RegisterID::kARMv8_x29, .name = "x29", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x29, .bits = 64, .dwarf_id = 29},
    {.id = RegisterID::kARMv8_lr,  .name = "lr",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_lr,  .bits = 64, .dwarf_id = 30},
    {.id = RegisterID::kARMv8_tpidr,  .name = "tpidr",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_tpidr,  .bits = 64, .dwarf_id = 128},
    {.id = RegisterID::kARMv8_sp,  .name = "sp",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_sp,  .bits = 64, .dwarf_id = 31},
    {.id = RegisterID::kARMv8_pc,  .name = "pc",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_pc,  .bits = 64},

    {.id = RegisterID::kARMv8_cpsr, .name = "cpsr", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_cpsr, .bits = 64},

    // FP (none defined for ARM64).

    // Vector.

    {.id = RegisterID::kARMv8_fpcr, .name = "fpcr", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_fpcr, .bits = 32},
    {.id = RegisterID::kARMv8_fpsr, .name = "fpsr", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_fpsr, .bits = 32},

    {.id = RegisterID::kARMv8_v0,  .name = "v0",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v0,  .bits = 128, .dwarf_id = 64},
    {.id = RegisterID::kARMv8_v1,  .name = "v1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v1,  .bits = 128, .dwarf_id = 65},
    {.id = RegisterID::kARMv8_v2,  .name = "v2",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v2,  .bits = 128, .dwarf_id = 66},
    {.id = RegisterID::kARMv8_v3,  .name = "v3",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v3,  .bits = 128, .dwarf_id = 67},
    {.id = RegisterID::kARMv8_v4,  .name = "v4",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v4,  .bits = 128, .dwarf_id = 68},
    {.id = RegisterID::kARMv8_v5,  .name = "v5",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v5,  .bits = 128, .dwarf_id = 69},
    {.id = RegisterID::kARMv8_v6,  .name = "v6",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v6,  .bits = 128, .dwarf_id = 70},
    {.id = RegisterID::kARMv8_v7,  .name = "v7",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v7,  .bits = 128, .dwarf_id = 71},
    {.id = RegisterID::kARMv8_v8,  .name = "v8",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v8,  .bits = 128, .dwarf_id = 72},
    {.id = RegisterID::kARMv8_v9,  .name = "v9",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v9,  .bits = 128, .dwarf_id = 73},
    {.id = RegisterID::kARMv8_v10, .name = "v10", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v10, .bits = 128, .dwarf_id = 74},
    {.id = RegisterID::kARMv8_v11, .name = "v11", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v11, .bits = 128, .dwarf_id = 75},
    {.id = RegisterID::kARMv8_v12, .name = "v12", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v12, .bits = 128, .dwarf_id = 76},
    {.id = RegisterID::kARMv8_v13, .name = "v13", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v13, .bits = 128, .dwarf_id = 77},
    {.id = RegisterID::kARMv8_v14, .name = "v14", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v14, .bits = 128, .dwarf_id = 78},
    {.id = RegisterID::kARMv8_v15, .name = "v15", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v15, .bits = 128, .dwarf_id = 79},
    {.id = RegisterID::kARMv8_v16, .name = "v16", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v16, .bits = 128, .dwarf_id = 80},
    {.id = RegisterID::kARMv8_v17, .name = "v17", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v17, .bits = 128, .dwarf_id = 81},
    {.id = RegisterID::kARMv8_v18, .name = "v18", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v18, .bits = 128, .dwarf_id = 82},
    {.id = RegisterID::kARMv8_v19, .name = "v19", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v19, .bits = 128, .dwarf_id = 83},
    {.id = RegisterID::kARMv8_v20, .name = "v20", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v20, .bits = 128, .dwarf_id = 84},
    {.id = RegisterID::kARMv8_v21, .name = "v21", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v21, .bits = 128, .dwarf_id = 85},
    {.id = RegisterID::kARMv8_v22, .name = "v22", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v22, .bits = 128, .dwarf_id = 86},
    {.id = RegisterID::kARMv8_v23, .name = "v23", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v23, .bits = 128, .dwarf_id = 87},
    {.id = RegisterID::kARMv8_v24, .name = "v24", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v24, .bits = 128, .dwarf_id = 88},
    {.id = RegisterID::kARMv8_v25, .name = "v25", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v25, .bits = 128, .dwarf_id = 89},
    {.id = RegisterID::kARMv8_v26, .name = "v26", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v26, .bits = 128, .dwarf_id = 90},
    {.id = RegisterID::kARMv8_v27, .name = "v27", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v27, .bits = 128, .dwarf_id = 91},
    {.id = RegisterID::kARMv8_v28, .name = "v28", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v28, .bits = 128, .dwarf_id = 92},
    {.id = RegisterID::kARMv8_v29, .name = "v29", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v29, .bits = 128, .dwarf_id = 93},
    {.id = RegisterID::kARMv8_v30, .name = "v30", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v30, .bits = 128, .dwarf_id = 94},
    {.id = RegisterID::kARMv8_v31, .name = "v31", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_v31, .bits = 128, .dwarf_id = 95},

    // Debug.

    {.id = RegisterID::kARMv8_id_aa64dfr0_el1, .name = "id_aa64dfr0_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_id_aa64dfr0_el1, .bits = 64},
    {.id = RegisterID::kARMv8_mdscr_el1,       .name = "mdscr_el1",       .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_mdscr_el1,       .bits = 64},

    {.id = RegisterID::kARMv8_dbgbcr0_el1,  .name = "dbgbcr0_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr0_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr1_el1,  .name = "dbgbcr1_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr1_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr2_el1,  .name = "dbgbcr2_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr2_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr3_el1,  .name = "dbgbcr3_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr3_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr4_el1,  .name = "dbgbcr4_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr4_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr5_el1,  .name = "dbgbcr5_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr5_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr6_el1,  .name = "dbgbcr6_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr6_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr7_el1,  .name = "dbgbcr7_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr7_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr8_el1,  .name = "dbgbcr8_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr8_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr9_el1,  .name = "dbgbcr9_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr9_el1,  .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr10_el1, .name = "dbgbcr10_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr10_el1, .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr11_el1, .name = "dbgbcr11_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr11_el1, .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr12_el1, .name = "dbgbcr12_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr12_el1, .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr13_el1, .name = "dbgbcr13_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr13_el1, .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr14_el1, .name = "dbgbcr14_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr14_el1, .bits = 32},
    {.id = RegisterID::kARMv8_dbgbcr15_el1, .name = "dbgbcr15_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbcr15_el1, .bits = 32},

    {.id = RegisterID::kARMv8_dbgbvr0_el1,  .name = "dbgbvr0_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr0_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr1_el1,  .name = "dbgbvr1_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr1_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr2_el1,  .name = "dbgbvr2_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr2_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr3_el1,  .name = "dbgbvr3_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr3_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr4_el1,  .name = "dbgbvr4_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr4_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr5_el1,  .name = "dbgbvr5_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr5_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr6_el1,  .name = "dbgbvr6_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr6_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr7_el1,  .name = "dbgbvr7_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr7_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr8_el1,  .name = "dbgbvr8_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr8_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr9_el1,  .name = "dbgbvr9_el1",  .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr9_el1,  .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr10_el1, .name = "dbgbvr10_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr10_el1, .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr11_el1, .name = "dbgbvr11_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr11_el1, .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr12_el1, .name = "dbgbvr12_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr12_el1, .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr13_el1, .name = "dbgbvr13_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr13_el1, .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr14_el1, .name = "dbgbvr14_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr14_el1, .bits = 64},
    {.id = RegisterID::kARMv8_dbgbvr15_el1, .name = "dbgbvr15_el1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_dbgbvr15_el1, .bits = 64},

    // General-purpose aliases.

    // Our canonical name for x30 is "LR".
    {.id = RegisterID::kARMv8_x30, .name = "x30", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_lr, .bits = 64},

    {.id = RegisterID::kARMv8_w0, .name = "w0", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x0, .bits = 32},
    {.id = RegisterID::kARMv8_w1, .name = "w1", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x1, .bits = 32},
    {.id = RegisterID::kARMv8_w2, .name = "w2", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x2, .bits = 32},
    {.id = RegisterID::kARMv8_w3, .name = "w3", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x3, .bits = 32},
    {.id = RegisterID::kARMv8_w4, .name = "w4", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x4, .bits = 32},
    {.id = RegisterID::kARMv8_w5, .name = "w5", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x5, .bits = 32},
    {.id = RegisterID::kARMv8_w6, .name = "w6", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x6, .bits = 32},
    {.id = RegisterID::kARMv8_w7, .name = "w7", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x7, .bits = 32},
    {.id = RegisterID::kARMv8_w8, .name = "w8", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x8, .bits = 32},
    {.id = RegisterID::kARMv8_w9, .name = "w9", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x9, .bits = 32},
    {.id = RegisterID::kARMv8_w10, .name = "w10", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x10, .bits = 32},
    {.id = RegisterID::kARMv8_w11, .name = "w11", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x11, .bits = 32},
    {.id = RegisterID::kARMv8_w12, .name = "w12", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x12, .bits = 32},
    {.id = RegisterID::kARMv8_w13, .name = "w13", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x13, .bits = 32},
    {.id = RegisterID::kARMv8_w14, .name = "w14", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x14, .bits = 32},
    {.id = RegisterID::kARMv8_w15, .name = "w15", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x15, .bits = 32},
    {.id = RegisterID::kARMv8_w16, .name = "w16", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x16, .bits = 32},
    {.id = RegisterID::kARMv8_w17, .name = "w17", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x17, .bits = 32},
    {.id = RegisterID::kARMv8_w18, .name = "w18", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x18, .bits = 32},
    {.id = RegisterID::kARMv8_w19, .name = "w19", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x19, .bits = 32},
    {.id = RegisterID::kARMv8_w20, .name = "w20", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x20, .bits = 32},
    {.id = RegisterID::kARMv8_w21, .name = "w21", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x21, .bits = 32},
    {.id = RegisterID::kARMv8_w22, .name = "w22", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x22, .bits = 32},
    {.id = RegisterID::kARMv8_w23, .name = "w23", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x23, .bits = 32},
    {.id = RegisterID::kARMv8_w24, .name = "w24", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x24, .bits = 32},
    {.id = RegisterID::kARMv8_w25, .name = "w25", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x25, .bits = 32},
    {.id = RegisterID::kARMv8_w26, .name = "w26", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x26, .bits = 32},
    {.id = RegisterID::kARMv8_w27, .name = "w27", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x27, .bits = 32},
    {.id = RegisterID::kARMv8_w28, .name = "w28", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x28, .bits = 32},
    {.id = RegisterID::kARMv8_w29, .name = "w29", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x29, .bits = 32},
    {.id = RegisterID::kARMv8_w30, .name = "w30", .arch = Arch::kArm64, .canonical_id = RegisterID::kARMv8_x30, .bits = 32},

    // x64
    // ---------------------------------------------------------------------------------------------

    // General purpose.

    {.id = RegisterID::kX64_rax, .name = "rax", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rax, .bits = 64, .dwarf_id = 0},
    {.id = RegisterID::kX64_rbx, .name = "rbx", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rbx, .bits = 64, .dwarf_id = 3},
    {.id = RegisterID::kX64_rcx, .name = "rcx", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rcx, .bits = 64, .dwarf_id = 2},
    {.id = RegisterID::kX64_rdx, .name = "rdx", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdx, .bits = 64, .dwarf_id = 1},
    {.id = RegisterID::kX64_rsi, .name = "rsi", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rsi, .bits = 64, .dwarf_id = 4},
    {.id = RegisterID::kX64_rdi, .name = "rdi", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdi, .bits = 64, .dwarf_id = 5},
    {.id = RegisterID::kX64_rbp, .name = "rbp", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rbp, .bits = 64, .dwarf_id = 6},
    {.id = RegisterID::kX64_rsp, .name = "rsp", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rsp, .bits = 64, .dwarf_id = 7},
    {.id = RegisterID::kX64_r8,  .name = "r8",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r8,  .bits = 64, .dwarf_id = 8},
    {.id = RegisterID::kX64_r9,  .name = "r9",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r9,  .bits = 64, .dwarf_id = 9},
    {.id = RegisterID::kX64_r10, .name = "r10", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r10, .bits = 64, .dwarf_id = 10},
    {.id = RegisterID::kX64_r11, .name = "r11", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r11, .bits = 64, .dwarf_id = 11},
    {.id = RegisterID::kX64_r12, .name = "r12", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r12, .bits = 64, .dwarf_id = 12},
    {.id = RegisterID::kX64_r13, .name = "r13", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r13, .bits = 64, .dwarf_id = 13},
    {.id = RegisterID::kX64_r14, .name = "r14", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r14, .bits = 64, .dwarf_id = 14},
    {.id = RegisterID::kX64_r15, .name = "r15", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_r15, .bits = 64, .dwarf_id = 15},
    {.id = RegisterID::kX64_rip, .name = "rip", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rip, .bits = 64},

    {.id = RegisterID::kX64_rflags, .name = "rflags", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rflags, .bits = 64, .dwarf_id = 49},
    {.id = RegisterID::kX64_fsbase, .name = "fsbase", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_fsbase, .bits = 64, .dwarf_id = 58},

    // General-purpose aliases.

    {.id = RegisterID::kX64_ah,  .name = "ah",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rax, .bits = 8, .shift = 8},
    {.id = RegisterID::kX64_al,  .name = "al",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rax, .bits = 8},
    {.id = RegisterID::kX64_ax,  .name = "ax",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rax, .bits = 16},
    {.id = RegisterID::kX64_eax, .name = "eax", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rax, .bits = 32},

    {.id = RegisterID::kX64_bh,  .name = "bh",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rbx, .bits = 8, .shift = 8},
    {.id = RegisterID::kX64_bl,  .name = "bl",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rbx, .bits = 8},
    {.id = RegisterID::kX64_bx,  .name = "bx",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rbx, .bits = 16},
    {.id = RegisterID::kX64_ebx, .name = "ebx", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rbx, .bits = 32},

    {.id = RegisterID::kX64_ch,  .name = "ch",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rcx, .bits = 8, .shift = 8},
    {.id = RegisterID::kX64_cl,  .name = "cl",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rcx, .bits = 8},
    {.id = RegisterID::kX64_cx,  .name = "cx",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rcx, .bits = 16},
    {.id = RegisterID::kX64_ecx, .name = "ecx", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rcx, .bits = 32},

    {.id = RegisterID::kX64_dh,  .name = "dh",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdx, .bits = 8, .shift = 8},
    {.id = RegisterID::kX64_dl,  .name = "dl",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdx, .bits = 8},
    {.id = RegisterID::kX64_dx,  .name = "dx",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdx, .bits = 16},
    {.id = RegisterID::kX64_edx, .name = "edx", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdx, .bits = 32},

    {.id = RegisterID::kX64_si,  .name = "si",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rsi, .bits = 16},
    {.id = RegisterID::kX64_esi, .name = "esi", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rsi, .bits = 32},

    {.id = RegisterID::kX64_di,  .name = "di",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdi, .bits = 16},
    {.id = RegisterID::kX64_edi, .name = "edi", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_rdi, .bits = 32},

    // Note we don't have an entry for bp/ebp, sp/esp, and ip/eip because these are all pointers
    // and the low bits are more likely to be user error (they wanted the whole thing) and we don't
    // want to be misleading in those cases.
    // FP.

    {.id = RegisterID::kX64_fcw, .name = "fcw", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_fcw, .bits = 16, .dwarf_id = 65},
    {.id = RegisterID::kX64_fsw, .name = "fsw", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_fsw, .bits = 16, .dwarf_id = 66},
    {.id = RegisterID::kX64_ftw, .name = "ftw", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_ftw, .bits = 16},
    {.id = RegisterID::kX64_fop, .name = "fop", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_fop, .bits = 16},  // 11 valid bits
    {.id = RegisterID::kX64_fip, .name = "fip", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_fip, .bits = 64},
    {.id = RegisterID::kX64_fdp, .name = "fdp", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_fdp, .bits = 64},

    {.id = RegisterID::kX64_st0, .name = "st0", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st0, .bits = 80, .dwarf_id = 33},
    {.id = RegisterID::kX64_st1, .name = "st1", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st1, .bits = 80, .dwarf_id = 34},
    {.id = RegisterID::kX64_st2, .name = "st2", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st2, .bits = 80, .dwarf_id = 35},
    {.id = RegisterID::kX64_st3, .name = "st3", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st3, .bits = 80, .dwarf_id = 36},
    {.id = RegisterID::kX64_st4, .name = "st4", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st4, .bits = 80, .dwarf_id = 37},
    {.id = RegisterID::kX64_st5, .name = "st5", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st5, .bits = 80, .dwarf_id = 38},
    {.id = RegisterID::kX64_st6, .name = "st6", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st6, .bits = 80, .dwarf_id = 39},
    {.id = RegisterID::kX64_st7, .name = "st7", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st7, .bits = 80, .dwarf_id = 40},

    // Vector.

    {.id = RegisterID::kX64_mxcsr, .name = "mxcsr", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_mxcsr, .bits = 32, .dwarf_id = 64},

    // AVX-512 (our canonical vector register names).
    {.id = RegisterID::kX64_zmm0,  .name = "zmm0",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm0,  .bits = 512},
    {.id = RegisterID::kX64_zmm1,  .name = "zmm1",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm1,  .bits = 512},
    {.id = RegisterID::kX64_zmm2,  .name = "zmm2",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm2,  .bits = 512},
    {.id = RegisterID::kX64_zmm3,  .name = "zmm3",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm3,  .bits = 512},
    {.id = RegisterID::kX64_zmm4,  .name = "zmm4",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm4,  .bits = 512},
    {.id = RegisterID::kX64_zmm5,  .name = "zmm5",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm5,  .bits = 512},
    {.id = RegisterID::kX64_zmm6,  .name = "zmm6",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm6,  .bits = 512},
    {.id = RegisterID::kX64_zmm7,  .name = "zmm7",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm7,  .bits = 512},
    {.id = RegisterID::kX64_zmm8,  .name = "zmm8",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm8,  .bits = 512},
    {.id = RegisterID::kX64_zmm9,  .name = "zmm9",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm9,  .bits = 512},
    {.id = RegisterID::kX64_zmm10, .name = "zmm10", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm10, .bits = 512},
    {.id = RegisterID::kX64_zmm11, .name = "zmm11", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm11, .bits = 512},
    {.id = RegisterID::kX64_zmm12, .name = "zmm12", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm12, .bits = 512},
    {.id = RegisterID::kX64_zmm13, .name = "zmm13", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm13, .bits = 512},
    {.id = RegisterID::kX64_zmm14, .name = "zmm14", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm14, .bits = 512},
    {.id = RegisterID::kX64_zmm15, .name = "zmm15", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm15, .bits = 512},
    {.id = RegisterID::kX64_zmm16, .name = "zmm16", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm16, .bits = 512},
    {.id = RegisterID::kX64_zmm17, .name = "zmm17", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm17, .bits = 512},
    {.id = RegisterID::kX64_zmm18, .name = "zmm18", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm18, .bits = 512},
    {.id = RegisterID::kX64_zmm19, .name = "zmm19", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm19, .bits = 512},
    {.id = RegisterID::kX64_zmm20, .name = "zmm20", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm20, .bits = 512},
    {.id = RegisterID::kX64_zmm21, .name = "zmm21", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm21, .bits = 512},
    {.id = RegisterID::kX64_zmm22, .name = "zmm22", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm22, .bits = 512},
    {.id = RegisterID::kX64_zmm23, .name = "zmm23", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm23, .bits = 512},
    {.id = RegisterID::kX64_zmm24, .name = "zmm24", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm24, .bits = 512},
    {.id = RegisterID::kX64_zmm25, .name = "zmm25", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm25, .bits = 512},
    {.id = RegisterID::kX64_zmm26, .name = "zmm26", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm26, .bits = 512},
    {.id = RegisterID::kX64_zmm27, .name = "zmm27", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm27, .bits = 512},
    {.id = RegisterID::kX64_zmm28, .name = "zmm28", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm28, .bits = 512},
    {.id = RegisterID::kX64_zmm29, .name = "zmm29", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm29, .bits = 512},
    {.id = RegisterID::kX64_zmm30, .name = "zmm30", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm30, .bits = 512},
    {.id = RegisterID::kX64_zmm31, .name = "zmm31", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm31, .bits = 512},

    // Vector aliases

    {.id = RegisterID::kX64_xmm0,  .name = "xmm0", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm0,  .bits = 128, .dwarf_id = 17},
    {.id = RegisterID::kX64_xmm1,  .name = "xmm1", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm1,  .bits = 128, .dwarf_id = 18},
    {.id = RegisterID::kX64_xmm2,  .name = "xmm2", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm2,  .bits = 128, .dwarf_id = 19},
    {.id = RegisterID::kX64_xmm3,  .name = "xmm3", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm3,  .bits = 128, .dwarf_id = 20},
    {.id = RegisterID::kX64_xmm4,  .name = "xmm4", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm4,  .bits = 128, .dwarf_id = 21},
    {.id = RegisterID::kX64_xmm5,  .name = "xmm5", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm5,  .bits = 128, .dwarf_id = 22},
    {.id = RegisterID::kX64_xmm6,  .name = "xmm6", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm6,  .bits = 128, .dwarf_id = 23},
    {.id = RegisterID::kX64_xmm7,  .name = "xmm7", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm7,  .bits = 128, .dwarf_id = 24},
    {.id = RegisterID::kX64_xmm8,  .name = "xmm8", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm8,  .bits = 128, .dwarf_id = 25},
    {.id = RegisterID::kX64_xmm9,  .name = "xmm9", .arch = Arch::kX64,  .canonical_id = RegisterID::kX64_zmm9,  .bits = 128, .dwarf_id = 26},
    {.id = RegisterID::kX64_xmm10, .name = "xmm10", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm10, .bits = 128, .dwarf_id = 27},
    {.id = RegisterID::kX64_xmm11, .name = "xmm11", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm11, .bits = 128, .dwarf_id = 28},
    {.id = RegisterID::kX64_xmm12, .name = "xmm12", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm12, .bits = 128, .dwarf_id = 29},
    {.id = RegisterID::kX64_xmm13, .name = "xmm13", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm13, .bits = 128, .dwarf_id = 30},
    {.id = RegisterID::kX64_xmm14, .name = "xmm14", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm14, .bits = 128, .dwarf_id = 31},
    {.id = RegisterID::kX64_xmm15, .name = "xmm15", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm15, .bits = 128, .dwarf_id = 32},
    {.id = RegisterID::kX64_xmm16, .name = "xmm16", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm16, .bits = 128, .dwarf_id = 67},
    {.id = RegisterID::kX64_xmm17, .name = "xmm17", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm17, .bits = 128, .dwarf_id = 68},
    {.id = RegisterID::kX64_xmm18, .name = "xmm18", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm18, .bits = 128, .dwarf_id = 69},
    {.id = RegisterID::kX64_xmm19, .name = "xmm19", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm19, .bits = 128, .dwarf_id = 70},
    {.id = RegisterID::kX64_xmm20, .name = "xmm20", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm20, .bits = 128, .dwarf_id = 71},
    {.id = RegisterID::kX64_xmm21, .name = "xmm21", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm21, .bits = 128, .dwarf_id = 72},
    {.id = RegisterID::kX64_xmm22, .name = "xmm22", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm22, .bits = 128, .dwarf_id = 73},
    {.id = RegisterID::kX64_xmm23, .name = "xmm23", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm23, .bits = 128, .dwarf_id = 74},
    {.id = RegisterID::kX64_xmm24, .name = "xmm24", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm24, .bits = 128, .dwarf_id = 75},
    {.id = RegisterID::kX64_xmm25, .name = "xmm25", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm25, .bits = 128, .dwarf_id = 76},
    {.id = RegisterID::kX64_xmm26, .name = "xmm26", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm26, .bits = 128, .dwarf_id = 77},
    {.id = RegisterID::kX64_xmm27, .name = "xmm27", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm27, .bits = 128, .dwarf_id = 78},
    {.id = RegisterID::kX64_xmm28, .name = "xmm28", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm28, .bits = 128, .dwarf_id = 79},
    {.id = RegisterID::kX64_xmm29, .name = "xmm29", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm29, .bits = 128, .dwarf_id = 80},
    {.id = RegisterID::kX64_xmm30, .name = "xmm30", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm30, .bits = 128, .dwarf_id = 81},
    {.id = RegisterID::kX64_xmm31, .name = "xmm31", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm31, .bits = 128, .dwarf_id = 82},

    {.id = RegisterID::kX64_ymm0,  .name = "ymm0",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm0,  .bits = 256},
    {.id = RegisterID::kX64_ymm1,  .name = "ymm1",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm1,  .bits = 256},
    {.id = RegisterID::kX64_ymm2,  .name = "ymm2",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm2,  .bits = 256},
    {.id = RegisterID::kX64_ymm3,  .name = "ymm3",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm3,  .bits = 256},
    {.id = RegisterID::kX64_ymm4,  .name = "ymm4",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm4,  .bits = 256},
    {.id = RegisterID::kX64_ymm5,  .name = "ymm5",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm5,  .bits = 256},
    {.id = RegisterID::kX64_ymm6,  .name = "ymm6",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm6,  .bits = 256},
    {.id = RegisterID::kX64_ymm7,  .name = "ymm7",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm7,  .bits = 256},
    {.id = RegisterID::kX64_ymm8,  .name = "ymm8",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm8,  .bits = 256},
    {.id = RegisterID::kX64_ymm9,  .name = "ymm9",  .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm9,  .bits = 256},
    {.id = RegisterID::kX64_ymm10, .name = "ymm10", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm10, .bits = 256},
    {.id = RegisterID::kX64_ymm11, .name = "ymm11", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm11, .bits = 256},
    {.id = RegisterID::kX64_ymm12, .name = "ymm12", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm12, .bits = 256},
    {.id = RegisterID::kX64_ymm13, .name = "ymm13", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm13, .bits = 256},
    {.id = RegisterID::kX64_ymm14, .name = "ymm14", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm14, .bits = 256},
    {.id = RegisterID::kX64_ymm15, .name = "ymm15", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm15, .bits = 256},
    {.id = RegisterID::kX64_ymm16, .name = "ymm16", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm16, .bits = 256},
    {.id = RegisterID::kX64_ymm17, .name = "ymm17", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm17, .bits = 256},
    {.id = RegisterID::kX64_ymm18, .name = "ymm18", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm18, .bits = 256},
    {.id = RegisterID::kX64_ymm19, .name = "ymm19", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm19, .bits = 256},
    {.id = RegisterID::kX64_ymm20, .name = "ymm20", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm20, .bits = 256},
    {.id = RegisterID::kX64_ymm21, .name = "ymm21", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm21, .bits = 256},
    {.id = RegisterID::kX64_ymm22, .name = "ymm22", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm22, .bits = 256},
    {.id = RegisterID::kX64_ymm23, .name = "ymm23", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm23, .bits = 256},
    {.id = RegisterID::kX64_ymm24, .name = "ymm24", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm24, .bits = 256},
    {.id = RegisterID::kX64_ymm25, .name = "ymm25", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm25, .bits = 256},
    {.id = RegisterID::kX64_ymm26, .name = "ymm26", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm26, .bits = 256},
    {.id = RegisterID::kX64_ymm27, .name = "ymm27", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm27, .bits = 256},
    {.id = RegisterID::kX64_ymm28, .name = "ymm28", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm28, .bits = 256},
    {.id = RegisterID::kX64_ymm29, .name = "ymm29", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm29, .bits = 256},
    {.id = RegisterID::kX64_ymm30, .name = "ymm30", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm30, .bits = 256},
    {.id = RegisterID::kX64_ymm31, .name = "ymm31", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_zmm31, .bits = 256},

    // The old-style MMX registers are the low 64-bits of the FP registers.
    {.id = RegisterID::kX64_mm0, .name = "mm0", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st0, .bits = 64, .dwarf_id = 41},
    {.id = RegisterID::kX64_mm1, .name = "mm1", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st1, .bits = 64, .dwarf_id = 42},
    {.id = RegisterID::kX64_mm2, .name = "mm2", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st2, .bits = 64, .dwarf_id = 43},
    {.id = RegisterID::kX64_mm3, .name = "mm3", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st3, .bits = 64, .dwarf_id = 44},
    {.id = RegisterID::kX64_mm4, .name = "mm4", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st4, .bits = 64, .dwarf_id = 45},
    {.id = RegisterID::kX64_mm5, .name = "mm5", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st5, .bits = 64, .dwarf_id = 46},
    {.id = RegisterID::kX64_mm6, .name = "mm6", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st6, .bits = 64, .dwarf_id = 47},
    {.id = RegisterID::kX64_mm7, .name = "mm7", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_st7, .bits = 64, .dwarf_id = 48},

    // Debug.

    {.id = RegisterID::kX64_dr0, .name = "dr0", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_dr0, .bits = 64},
    {.id = RegisterID::kX64_dr1, .name = "dr1", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_dr1, .bits = 64},
    {.id = RegisterID::kX64_dr2, .name = "dr2", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_dr2, .bits = 64},
    {.id = RegisterID::kX64_dr3, .name = "dr3", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_dr3, .bits = 64},
    {.id = RegisterID::kX64_dr6, .name = "dr6", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_dr6, .bits = 64},
    {.id = RegisterID::kX64_dr7, .name = "dr7", .arch = Arch::kX64, .canonical_id = RegisterID::kX64_dr7, .bits = 64},
};

// clang-format on

// DWARF NOTES
//
// References
//
//   X64: https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
//        Page 62
//   ARM:
//   http://infocenter.arm.com/help/topic/com.arm.doc.ecm0665627/abi_sve_aadwarf_100985_0000_00_en.pdf
//        Page 6
//
// We don't have definitions yet of the following x86 DWARF registers:
//
//   50-55 -> (%es, %cs, %ss, %ds, %fs, %gs)
//   58 -> FS Base Address
//   59 -> GS Base Address
//   62 -> %ts (Task Register)
//   63 -> %ldtr
//   118-125 -> %k0–%k7 (Vector Mask Registers 0–7)
//   126-129 -> %bnd0–%bnd3 (Bound Registers 0–3)
//
// Nor the following ARM DWARF registers:
//
//   33 -> ELR_mode
//   46 -> VG 64-bit SVE Vector granule pseudo register
//   47 -> FFR VG´8-bit SVE first fault register
//   48-63 -> P0-P15 VG´8-bit SVE predicate registers
//   96-127 -> Z0-Z31 VG´64-bit SVE vector registers

}  // namespace

const RegisterInfo* InfoForRegister(RegisterID id) {
  static std::map<RegisterID, const RegisterInfo*> info_map;

  if (info_map.empty()) {
    for (const auto& info : kRegisterInfo) {
      FXL_DCHECK(info_map.find(info.id) == info_map.end());
      info_map[info.id] = &info;
    }
  }

  auto iter = info_map.find(id);
  if (iter != info_map.end())
    return iter->second;

  return nullptr;
}

const RegisterInfo* InfoForRegister(Arch arch, const std::string& name) {
  static std::map<std::pair<Arch, std::string>, const RegisterInfo*> info_map;

  if (info_map.empty()) {
    for (const auto& info : kRegisterInfo) {
      FXL_DCHECK(info_map.find(std::make_pair(info.arch, info.name)) == info_map.end());
      info_map[std::make_pair(info.arch, std::string(info.name))] = &info;
    }
  }

  auto iter = info_map.find(std::make_pair(arch, name));
  if (iter != info_map.end())
    return iter->second;

  return nullptr;
}

RegisterID GetSpecialRegisterID(Arch arch, SpecialRegisterType type) {
  switch (arch) {
    case Arch::kX64:
      switch (type) {
        case SpecialRegisterType::kNone:
          break;
        case SpecialRegisterType::kIP:
          return RegisterID::kX64_rip;
        case SpecialRegisterType::kSP:
          return RegisterID::kX64_rsp;
        case SpecialRegisterType::kTP:
          return RegisterID::kX64_fsbase;
      }
      break;

    case Arch::kArm64:
      switch (type) {
        case SpecialRegisterType::kNone:
          break;
        case SpecialRegisterType::kIP:
          return RegisterID::kARMv8_pc;
        case SpecialRegisterType::kSP:
          return RegisterID::kARMv8_sp;
        case SpecialRegisterType::kTP:
          return RegisterID::kARMv8_tpidr;
      }
      break;

    case Arch::kUnknown:
      break;
  }

  FXL_NOTREACHED();
  return RegisterID::kUnknown;
}

const char* RegisterIDToString(RegisterID id) {
  auto info = InfoForRegister(id);

  if (!info) {
    FXL_NOTREACHED() << "Unknown register requested: " << static_cast<uint32_t>(id);
    return "";
  }

  return info->name.c_str();
}

RegisterID StringToRegisterID(const std::string& reg) {
  static std::map<std::string, RegisterID> id_map;

  if (id_map.empty()) {
    // We populate the whole map at once, otherwise any time we try to look up
    // an invalid register ID (fairly often) we have to search the whole list.
    for (size_t i = 0; i < std::size(kRegisterInfo); i++) {
      id_map[kRegisterInfo[i].name] = kRegisterInfo[i].id;
    }
  }

  auto iter = id_map.find(reg);

  if (iter == id_map.end()) {
    return RegisterID::kUnknown;
  }

  return iter->second;
}

Arch GetArchForRegisterID(RegisterID id) {
  auto info = InfoForRegister(id);

  if (!info) {
    FXL_NOTREACHED() << "Arch for unknown register requested: " << static_cast<uint32_t>(id);
    return Arch::kUnknown;
  }

  return info->arch;
}

SpecialRegisterType GetSpecialRegisterType(RegisterID id) {
  switch (id) {
    case RegisterID::kX64_rip:
    case RegisterID::kARMv8_pc:
      return debug_ipc::SpecialRegisterType::kIP;
    case RegisterID::kX64_rsp:
    case RegisterID::kARMv8_sp:
      return debug_ipc::SpecialRegisterType::kSP;
    case RegisterID::kX64_fsbase:
    case RegisterID::kARMv8_tpidr:
      return debug_ipc::SpecialRegisterType::kTP;
    default:
      return debug_ipc::SpecialRegisterType::kNone;
  }
}

const RegisterInfo* DWARFToRegisterInfo(Arch arch, uint32_t dwarf_reg_id) {
  static std::map<std::pair<Arch, uint32_t>, const RegisterInfo*> info_map;

  if (info_map.empty()) {
    for (const auto& info : kRegisterInfo) {
      if (info.dwarf_id != RegisterInfo::kNoDwarfId) {
        FXL_DCHECK(info_map.find(std::make_pair(info.arch, info.dwarf_id)) == info_map.end());
        info_map[std::make_pair(info.arch, info.dwarf_id)] = &info;
      }
    }
  }

  auto iter = info_map.find(std::make_pair(arch, dwarf_reg_id));
  if (iter != info_map.end())
    return iter->second;

  return nullptr;
}

bool IsGeneralRegister(RegisterID id) {
  return (static_cast<uint32_t>(id) >= static_cast<uint32_t>(kARMv8GeneralBegin) &&
          static_cast<uint32_t>(id) <= static_cast<uint32_t>(kARMv8GeneralEnd)) ||
         (static_cast<uint32_t>(id) >= static_cast<uint32_t>(kX64GeneralBegin) &&
          static_cast<uint32_t>(id) <= static_cast<uint32_t>(kX64GeneralEnd));
}

const char* RegisterCategoryToString(RegisterCategory cat) {
  switch (cat) {
    case RegisterCategory::kGeneral:
      return "General Purpose";
    case RegisterCategory::kFloatingPoint:
      return "Floating Point";
    case RegisterCategory::kVector:
      return "Vector";
    case RegisterCategory::kDebug:
      return "Debug";
    case RegisterCategory::kNone:
    case RegisterCategory::kLast:
      break;
  }
  FXL_NOTREACHED();
  return nullptr;
}

RegisterCategory RegisterIDToCategory(RegisterID id) {
  uint32_t val = static_cast<uint32_t>(id);

  // ARM.
  if (val >= kARMv8GeneralBegin && val <= kARMv8GeneralEnd) {
    return RegisterCategory::kGeneral;
  } else if (val >= kARMv8VectorBegin && val <= kARMv8VectorEnd) {
    return RegisterCategory::kVector;
  } else if (val >= kARMv8DebugBegin && val <= kARMv8DebugEnd) {
    return RegisterCategory::kDebug;
  }

  // x64.
  if (val >= kX64GeneralBegin && val <= kX64GeneralEnd) {
    return RegisterCategory::kGeneral;
  } else if (val >= kX64FPBegin && val <= kX64FPEnd) {
    return RegisterCategory::kFloatingPoint;
  } else if (val >= kX64VectorBegin && val <= kX64VectorEnd) {
    return RegisterCategory::kVector;
  } else if (val >= kX64DebugBegin && val <= kX64DebugEnd) {
    return RegisterCategory::kDebug;
  }

  return RegisterCategory::kNone;
}

containers::array_view<uint8_t> GetRegisterData(const std::vector<Register>& regs, RegisterID id) {
  const RegisterInfo* info = InfoForRegister(id);
  if (!info)
    return containers::array_view<uint8_t>();

  const Register* found_canonical = nullptr;
  for (const auto& reg : regs) {
    if (reg.id == id)
      return reg.data;  // Prefer an exact match.
    if (reg.id == info->canonical_id) {
      found_canonical = &reg;
      break;
    }
  }

  if (!found_canonical)
    return containers::array_view<uint8_t>();

  // Here we found a canonical register match that's not the exact register being requested. Extract
  // the correct number of bits.

  // Expect everything to be a multiple of 8. Currently all of our processors' pseudoregisters have
  // this property.
  FXL_DCHECK(info->bits > 0);
  FXL_DCHECK(info->bits % 8 == 0);
  FXL_DCHECK(info->shift % 8 == 0);

  containers::array_view<uint8_t> result = found_canonical->data;

  // The shift is a trim from the left because we assume little-endian.
  return result.subview(info->shift / 8, info->bits / 8);
}

}  // namespace debug_ipc
