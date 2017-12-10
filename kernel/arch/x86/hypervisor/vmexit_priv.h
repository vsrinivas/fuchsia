// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

// clang-format off

// VM exit reasons.
enum class ExitReason : uint32_t {
    EXCEPTION                   = 0u,  // NMI is an exception too
    EXTERNAL_INTERRUPT          = 1u,
    INTERRUPT_WINDOW            = 7u,
    CPUID                       = 10u,
    HLT                         = 12u,
    VMCALL                      = 18u,
    IO_INSTRUCTION              = 30u,
    RDMSR                       = 31u,
    WRMSR                       = 32u,
    ENTRY_FAILURE_GUEST_STATE   = 33u,
    ENTRY_FAILURE_MSR_LOADING   = 34u,
    EPT_VIOLATION               = 48u,
    XSETBV                      = 55u,
};

// VM exit interruption type.
enum class InterruptionType : uint8_t {
    EXTERNAL_INTERRUPT          = 0u,
    NON_MASKABLE_INTERRUPT      = 2u,
    HARDWARE_EXCEPTION          = 3u,
    SOFTWARE_EXCEPTION          = 6u,
};

// X2APIC MSR addresses from Volume 3, Section 10.12.1.2.
enum class X2ApicMsr : uint64_t {
    ID                  = 0x802,
    VERSION             = 0x803,
    EOI                 = 0x80b,
    TPR                 = 0x808,
    LDR                 = 0x80d,
    SVR                 = 0x80f,
    ISR_31_0            = 0x810,
    ISR_63_32           = 0x811,
    ISR_95_64           = 0x812,
    ISR_127_96          = 0x813,
    ISR_159_128         = 0x814,
    ISR_191_160         = 0x815,
    ISR_223_192         = 0x816,
    ISR_255_224         = 0x817,
    TMR_31_0            = 0x818,
    TMR_63_32           = 0x819,
    TMR_95_64           = 0x81a,
    TMR_127_96          = 0x81b,
    TMR_159_128         = 0x81c,
    TMR_191_160         = 0x81d,
    TMR_223_192         = 0x81e,
    TMR_255_224         = 0x81f,
    IRR_31_0            = 0x820,
    IRR_63_32           = 0x821,
    IRR_95_64           = 0x822,
    IRR_127_96          = 0x823,
    IRR_159_128         = 0x824,
    IRR_191_160         = 0x825,
    IRR_223_192         = 0x826,
    IRR_255_224         = 0x827,
    ESR                 = 0x828,
    ICR                 = 0x830,
    LVT_TIMER           = 0x832,
    LVT_MONITOR         = 0x834,
    LVT_LINT0           = 0x835,
    LVT_LINT1           = 0x836,
    LVT_ERROR           = 0x837,
    INITIAL_COUNT       = 0x838,
    DCR                 = 0x83e,
    SELF_IPI            = 0x83f,
};

// clang-format on

typedef struct zx_port_packet zx_port_packet_t;

class AutoVmcs;
class GuestPhysicalAddressSpace;
struct GuestState;
struct LocalApicState;
class TrapMap;

// Stores VM exit info from VMCS fields.
struct ExitInfo {
    bool entry_failure;
    ExitReason exit_reason;
    uint64_t exit_qualification;
    uint32_t exit_instruction_length;
    uint64_t guest_physical_address;
    uint64_t guest_rip;

    ExitInfo(const AutoVmcs& vmcs);
};

// Stores VM exit interruption information. See Volume 3, Section 24.9.2.
struct ExitInterruptionInformation {
    uint8_t vector;
    InterruptionType interruption_type;
    bool valid;

    ExitInterruptionInformation(const AutoVmcs& vmcs);
};

// Stores EPT violation info from the VMCS exit qualification field.
struct EptViolationInfo {
    bool read;
    bool write;
    bool instruction;

    EptViolationInfo(uint64_t qualification);
};

// Stores IO instruction info from the VMCS exit qualification field.
struct IoInfo {
    uint8_t access_size;
    bool input;
    bool string;
    bool repeat;
    uint16_t port;

    IoInfo(uint64_t qualification);
};

zx_status_t vmexit_handler(AutoVmcs* vmcs, GuestState* guest_state,
                           LocalApicState* local_apic_state, GuestPhysicalAddressSpace* gpas,
                           TrapMap* traps, zx_port_packet_t* packet);
