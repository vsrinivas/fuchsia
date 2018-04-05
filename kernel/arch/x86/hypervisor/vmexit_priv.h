// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/trap_map.h>
#include <zircon/types.h>

// clang-format off

// VM exit reasons.
enum class ExitReason : uint32_t {
    EXCEPTION                   = 0u,  // NMI is an exception too
    EXTERNAL_INTERRUPT          = 1u,
    TRIPLE_FAULT                = 2u,
    INIT_SIGNAL                 = 3u,
    STARTUP_IPI                 = 4u,
    IO_SMI                      = 5u,
    OTHER_SMI                   = 6u,
    INTERRUPT_WINDOW            = 7u,
    NMI_WINDOW                  = 8u,
    TASK_SWITCH                 = 9u,
    CPUID                       = 10u,
    GETSEC                      = 11u,
    HLT                         = 12u,
    INVD                        = 13u,
    INVLPG                      = 14u,
    RDPMC                       = 15u,
    RDTSC                       = 16u,
    RSM                         = 17u,
    VMCALL                      = 18u,
    VMCLEAR                     = 19u,
    VMLAUNCH                    = 20u,
    VMPTRLD                     = 21u,
    VMPTRST                     = 22u,
    VMREAD                      = 23u,
    VMRESUME                    = 24u,
    VMWRITE                     = 25u,
    VMXOFF                      = 26u,
    VMXON                       = 27u,
    CONTROL_REGISTER_ACCESS     = 28u,
    MOV_DR                      = 29u,
    IO_INSTRUCTION              = 30u,
    RDMSR                       = 31u,
    WRMSR                       = 32u,
    ENTRY_FAILURE_GUEST_STATE   = 33u,
    ENTRY_FAILURE_MSR_LOADING   = 34u,
    MWAIT                       = 36u,
    MONITOR_TRAP_FLAG           = 37u,
    MONITOR                     = 39u,
    PAUSE                       = 40u,
    ENTRY_FAILURE_MACHINE_CHECK = 41u,
    TPR_BELOW_THRESHOLD         = 43u,
    APIC_ACCESS                 = 44u,
    VIRTUALIZED_EOI             = 45u,
    ACCESS_GDTR_OR_IDTR         = 46u,
    ACCESS_LDTR_OR_TR           = 47u,
    EPT_VIOLATION               = 48u,
    EPT_MISCONFIGURATION        = 49u,
    INVEPT                      = 50u,
    RDTSCP                      = 51u,
    VMX_PREEMPT_TIMER_EXPIRED   = 52u,
    INVVPID                     = 53u,
    WBINVD                      = 54u,
    XSETBV                      = 55u,
    APIC_WRITE                  = 56u,
    RDRAND                      = 57u,
    INVPCID                     = 58u,
    VMFUNC                      = 59u,
    ENCLS                       = 60u,
    RDSEED                      = 61u,
    PAGE_MODIFICATION_LOG_FULL  = 62u,
    XSAVES                      = 63u,
    XRSTORS                     = 64u,
};

static inline const char* exit_reason_name(ExitReason exit_reason) {
#define EXIT_REASON_NAME(name) case ExitReason::name: return #name
    switch (exit_reason) {
    EXIT_REASON_NAME(EXCEPTION);
    EXIT_REASON_NAME(EXTERNAL_INTERRUPT);
    EXIT_REASON_NAME(TRIPLE_FAULT);
    EXIT_REASON_NAME(INIT_SIGNAL);
    EXIT_REASON_NAME(STARTUP_IPI);
    EXIT_REASON_NAME(IO_SMI);
    EXIT_REASON_NAME(OTHER_SMI);
    EXIT_REASON_NAME(INTERRUPT_WINDOW);
    EXIT_REASON_NAME(NMI_WINDOW);
    EXIT_REASON_NAME(TASK_SWITCH);
    EXIT_REASON_NAME(CPUID);
    EXIT_REASON_NAME(GETSEC);
    EXIT_REASON_NAME(HLT);
    EXIT_REASON_NAME(INVD);
    EXIT_REASON_NAME(INVLPG);
    EXIT_REASON_NAME(RDPMC);
    EXIT_REASON_NAME(RDTSC);
    EXIT_REASON_NAME(RSM);
    EXIT_REASON_NAME(VMCALL);
    EXIT_REASON_NAME(VMCLEAR);
    EXIT_REASON_NAME(VMLAUNCH);
    EXIT_REASON_NAME(VMPTRLD);
    EXIT_REASON_NAME(VMPTRST);
    EXIT_REASON_NAME(VMREAD);
    EXIT_REASON_NAME(VMRESUME);
    EXIT_REASON_NAME(VMWRITE);
    EXIT_REASON_NAME(VMXOFF);
    EXIT_REASON_NAME(VMXON);
    EXIT_REASON_NAME(CONTROL_REGISTER_ACCESS);
    EXIT_REASON_NAME(MOV_DR);
    EXIT_REASON_NAME(IO_INSTRUCTION);
    EXIT_REASON_NAME(RDMSR);
    EXIT_REASON_NAME(WRMSR);
    EXIT_REASON_NAME(ENTRY_FAILURE_GUEST_STATE);
    EXIT_REASON_NAME(ENTRY_FAILURE_MSR_LOADING);
    EXIT_REASON_NAME(MWAIT);
    EXIT_REASON_NAME(MONITOR_TRAP_FLAG);
    EXIT_REASON_NAME(MONITOR);
    EXIT_REASON_NAME(PAUSE);
    EXIT_REASON_NAME(ENTRY_FAILURE_MACHINE_CHECK);
    EXIT_REASON_NAME(TPR_BELOW_THRESHOLD);
    EXIT_REASON_NAME(APIC_ACCESS);
    EXIT_REASON_NAME(VIRTUALIZED_EOI);
    EXIT_REASON_NAME(ACCESS_GDTR_OR_IDTR);
    EXIT_REASON_NAME(ACCESS_LDTR_OR_TR);
    EXIT_REASON_NAME(EPT_VIOLATION);
    EXIT_REASON_NAME(EPT_MISCONFIGURATION);
    EXIT_REASON_NAME(INVEPT);
    EXIT_REASON_NAME(RDTSCP);
    EXIT_REASON_NAME(VMX_PREEMPT_TIMER_EXPIRED);
    EXIT_REASON_NAME(INVVPID);
    EXIT_REASON_NAME(WBINVD);
    EXIT_REASON_NAME(XSETBV);
    EXIT_REASON_NAME(APIC_WRITE);
    EXIT_REASON_NAME(RDRAND);
    EXIT_REASON_NAME(INVPCID);
    EXIT_REASON_NAME(VMFUNC);
    EXIT_REASON_NAME(ENCLS);
    EXIT_REASON_NAME(RDSEED);
    EXIT_REASON_NAME(PAGE_MODIFICATION_LOG_FULL);
    EXIT_REASON_NAME(XSAVES);
    EXIT_REASON_NAME(XRSTORS);
#undef EXIT_REASON_NAME
    default: return "UNKNOWN";
    }
}

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
    LVT_CMCI            = 0x82f,
    ICR                 = 0x830,
    LVT_TIMER           = 0x832,
    LVT_THERMAL_SENSOR  = 0x833,
    LVT_MONITOR         = 0x834,
    LVT_LINT0           = 0x835,
    LVT_LINT1           = 0x836,
    LVT_ERROR           = 0x837,
    INITIAL_COUNT       = 0x838,
    DCR                 = 0x83e,
    SELF_IPI            = 0x83f,
};

enum class InterruptDeliveryMode : uint8_t {
    FIXED               = 0u,
    SMI                 = 2u,
    NMI                 = 4u,
    INIT                = 5u,
    STARTUP             = 6u,
};

enum class InterruptDestinationMode : bool {
    PHYSICAL            = false,
    LOGICAL             = true,
};

enum class InterruptDestinationShorthand : uint8_t {
    NO_SHORTHAND        = 0u,
    SELF                = 1u,
    ALL_INCLUDING_SELF  = 2u,
    ALL_EXCLUDING_SELF  = 3u,
};

enum class CrAccessType : uint8_t {
    MOV_TO_CR           = 0u,
    MOV_FROM_CR         = 1u,
    CLTS                = 2u,
    LMSW                = 3u,
};

// clang-format on

typedef struct zx_port_packet zx_port_packet_t;

class AutoVmcs;
struct GuestState;
struct LocalApicState;
struct PvClockState;

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

// Stores control register access info from the VMCS exit qualification field.
struct CrAccessInfo {
    uint8_t cr_number;
    CrAccessType access_type;
    uint8_t reg;

    CrAccessInfo(uint64_t qualification);
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

// Interrupt command register.
struct InterruptCommandRegister {
    uint32_t destination;
    enum InterruptDestinationMode destination_mode;
    enum InterruptDeliveryMode delivery_mode;
    enum InterruptDestinationShorthand destination_shorthand;
    uint8_t vector;

    InterruptCommandRegister(uint32_t hi, uint32_t lo);
};

zx_status_t vmexit_handler(AutoVmcs* vmcs, GuestState* guest_state,
                           LocalApicState* local_apic_state, PvClockState* pvclock,
                           hypervisor::GuestPhysicalAddressSpace* gpas, hypervisor::TrapMap* traps,
                           zx_port_packet_t* packet);
