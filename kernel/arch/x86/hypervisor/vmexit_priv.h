// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

// clang-format off

/* VM exit reasons. */
enum class ExitReason : uint32_t {
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

class AutoVmcs;
class FifoDispatcher;
class GuestPhysicalAddressSpace;
struct GuestState;
struct IoApicState;
struct LocalApicState;
class PacketMux;
struct VmxState;

/* Stores VM exit info from VMCS fields. */
struct ExitInfo {
    ExitReason exit_reason;
    uint64_t exit_qualification;
    uint32_t instruction_length;
    uint64_t guest_physical_address;
    uint64_t guest_rip;

    ExitInfo(const AutoVmcs& vmcs);
};

/* Stores ept violation info from the VMCS exit qualification field. */
struct EptViolationInfo {
    bool read;
    bool write;
    bool instruction;
    bool present;

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
mx_status_t vmexit_handler(AutoVmcs* vmcs, GuestState* guest_state,
                           LocalApicState* local_apic_state, GuestPhysicalAddressSpace* gpas,
                           PacketMux& mux, mx_port_packet_t* packet);
