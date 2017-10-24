// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

// clang-format off

/* VM exit reasons. */
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
    APIC_ACCESS                 = 44u,
    EPT_VIOLATION               = 48u,
    XSETBV                      = 55u,
};

/* VM exit interruption type. */
enum class InterruptionType : uint8_t {
    EXTERNAL_INTERRUPT          = 0u,
    NON_MASKABLE_INTERRUPT      = 2u,
    HARDWARE_EXCEPTION          = 3u,
    SOFTWARE_EXCEPTION          = 6u,
};

/* APIC access types. */
enum class ApicAccessType : uint8_t {
    LINEAR_ACCESS_READ          = 0u,
    LINEAR_ACCESS_WRITE         = 1u,
    LINEAR_ACCESS_EXECUTE       = 2u,
    LINEAR_ACCESS_EVENT         = 3u,
    GUEST_PHYSICAL_EVENT        = 10u,
    GUEST_PHYSICAL_RWX          = 15u,
};

// clang-format on

typedef struct zx_port_packet zx_port_packet_t;

class AutoVmcs;
class GuestPhysicalAddressSpace;
struct GuestState;
struct LocalApicState;
class TrapMap;

/* Stores VM exit info from VMCS fields. */
struct ExitInfo {
    ExitReason exit_reason;
    bool vmentry_failure;
    uint64_t exit_qualification;
    uint32_t instruction_length;
    uint64_t guest_physical_address;
    uint64_t guest_rip;

    ExitInfo(const AutoVmcs& vmcs);
};

/* Sores VM exit interruption information (SDM 24.9.2). */
struct ExitInterruptionInformation {
    uint8_t vector;
    InterruptionType interruption_type;
    bool valid;

    ExitInterruptionInformation(const AutoVmcs& vmcs);
};

/* Stores ept violation info from the VMCS exit qualification field. */
struct EptViolationInfo {
    bool read;
    bool write;
    bool instruction;

    EptViolationInfo(uint64_t qualification);
};

/* Stores IO instruction info from the VMCS exit qualification field. */
struct IoInfo {
    uint8_t access_size;
    bool input;
    bool string;
    bool repeat;
    uint16_t port;

    IoInfo(uint64_t qualification);
};

/* Stores local APIC access info from the VMCS exit qualification field. */
struct ApicAccessInfo {
    uint16_t offset;
    ApicAccessType access_type;

    ApicAccessInfo(uint64_t qualification);
};

bool local_apic_signal_interrupt(LocalApicState* local_apic_state, uint32_t vector,
                                 bool reschedule);
zx_status_t vmexit_handler(AutoVmcs* vmcs, GuestState* guest_state,
                           LocalApicState* local_apic_state, GuestPhysicalAddressSpace* gpas,
                           TrapMap* traps, zx_port_packet_t* packet);
