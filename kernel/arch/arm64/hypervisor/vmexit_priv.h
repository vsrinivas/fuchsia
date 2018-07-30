// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/trap_map.h>
#include <zircon/types.h>

typedef struct zx_port_packet zx_port_packet_t;

struct GuestState;
struct GichState;

// clang-format off

// Exception class of an exception syndrome.
enum class ExceptionClass : uint8_t {
    WFI_WFE_INSTRUCTION = 0b000001,
    SMC_INSTRUCTION     = 0b010111,
    SYSTEM_INSTRUCTION  = 0b011000,
    INSTRUCTION_ABORT   = 0b100000,
    DATA_ABORT          = 0b100100,
};

// Exception syndrome for a VM exit.
struct ExceptionSyndrome {
    ExceptionClass ec;
    uint32_t iss;

    ExceptionSyndrome(uint32_t esr);
};

// Wait instruction that caused a VM exit.
struct WaitInstruction {
    bool is_wfe;

    WaitInstruction(uint32_t iss);
};

// SMC instruction that cause a VM exit.
struct SmcInstruction {
    uint16_t imm;

    SmcInstruction(uint32_t iss);
};

// System register associated with a system instruction.
enum class SystemRegister : uint16_t {
    MAIR_EL1        = 0b11000000 << 8 /* op */ | 0b10100010 /* cr */,
    SCTLR_EL1       = 0b11000000 << 8 /* op */ | 0b00010000 /* cr */,
    TCR_EL1         = 0b11010000 << 8 /* op */ | 0b00100000 /* cr */,
    TTBR0_EL1       = 0b11000000 << 8 /* op */ | 0b00100000 /* cr */,
    TTBR1_EL1       = 0b11001000 << 8 /* op */ | 0b00100000 /* cr */,

    // Debug Registers, trapped by MDCR_EL2.TDOSA = 1
    OSLAR_EL1       = 0b10100000 << 8 /* op */ | 0b00010000 /* cr */,
    OSLSR_EL1       = 0b10100000 << 8 /* op */ | 0b00010001 /* cr */,
    OSDLR_EL1       = 0b10100000 << 8 /* op */ | 0b00010011 /* cr */,
    DBGPRCR_EL1     = 0b10100000 << 8 /* op */ | 0b00010100 /* cr */,

    // Interrupt Controller System Registers. See GIC v3/v4 Architecture Spec Section 8.2.
    ICC_SGI1R_EL1   = 0b11101000 << 8 /* op */ | 0b11001011 /* cr */,
};

// System instruction that caused a VM exit.
struct SystemInstruction {
    SystemRegister sysreg;
    uint8_t xt;
    bool read;

    SystemInstruction(uint32_t iss);
};

struct SgiRegister {
    uint8_t aff3;
    uint8_t aff2;
    uint8_t aff1;
    uint8_t rs;
    uint16_t target_list;
    uint8_t int_id;
    bool all_but_local;

    SgiRegister(uint64_t sgi);
};

// Data abort that caused a VM exit.
struct DataAbort {
    bool valid;
    uint8_t access_size;
    bool sign_extend;
    uint8_t xt;
    bool read;

    DataAbort(uint32_t iss);
};

// clang-format on

zx_status_t vmexit_handler(uint64_t* hcr, GuestState* guest_state, GichState* gich_state,
                           hypervisor::GuestPhysicalAddressSpace* gpas, hypervisor::TrapMap* traps,
                           zx_port_packet_t* packet);
