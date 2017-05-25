// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

class AutoVmcsLoad;
class FifoDispatcher;
class GuestPhysicalAddressSpace;
struct GuestState;
struct IoApicState;
struct LocalApicState;
struct VmxState;

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

/* Stores VM exit info from VMCS fields. */
struct ExitInfo {
    ExitReason exit_reason;
    uint64_t exit_qualification;
    uint32_t instruction_length;
    uint64_t guest_physical_address;
    uint64_t guest_rip;

    ExitInfo();
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

    ApicAccessInfo(uint64_t qualification);
};

/* VM entry interruption type. */
enum class InterruptionType : uint32_t {
    EXTERNAL_INTERRUPT  = 0u,
    HARDWARE_EXCEPTION  = 3u,
};

void interrupt_window_exiting(bool enable);
status_t vmexit_handler(AutoVmcsLoad* vmcs_load, GuestState* guest_state,
                        LocalApicState* local_apic_state, GuestPhysicalAddressSpace* gpas,
                        FifoDispatcher* ctl_fifo);
