// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <hypervisor/packet_mux.h>

// clang-format off

#define X86_MSR_IA32_VMX_PINBASED_CTLS          0x0481 // Pin-based controls
#define X86_MSR_IA32_VMX_PROCBASED_CTLS         0x0482 // Primary processor-based controls
#define X86_MSR_IA32_VMX_EXIT_CTLS              0x0483 // VM-exit controls
#define X86_MSR_IA32_VMX_ENTRY_CTLS             0x0484 // VM-entry controls
#define X86_MSR_IA32_VMX_PROCBASED_CTLS2        0x048b // Secondary processor-based controls
#define X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS     0x048d // True pin-based controls
#define X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS    0x048e // True primary processor-based controls
#define X86_MSR_IA32_VMX_TRUE_EXIT_CTLS         0x048f // True VM-exit controls
#define X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS        0x0490 // True VM-entry controls

/* PROCBASED_CTLS2 flags */
#define PROCBASED_CTLS2_APIC_ACCESS             (1u << 0)
#define PROCBASED_CTLS2_EPT                     (1u << 1)
#define PROCBASED_CTLS2_RDTSCP                  (1u << 3)
#define PROCBASED_CTLS2_VPID                    (1u << 5)
#define PROCBASED_CTLS2_INVPCID                 (1u << 12)

/* PROCBASED_CTLS flags */
#define PROCBASED_CTLS_INT_WINDOW_EXITING       (1u << 2)
#define PROCBASED_CTLS_HLT_EXITING              (1u << 7)
#define PROCBASED_CTLS_CR3_LOAD_EXITING         (1u << 15)
#define PROCBASED_CTLS_CR3_STORE_EXITING        (1u << 16)
#define PROCBASED_CTLS_CR8_LOAD_EXITING         (1u << 19)
#define PROCBASED_CTLS_CR8_STORE_EXITING        (1u << 20)
#define PROCBASED_CTLS_TPR_SHADOW               (1u << 21)
#define PROCBASED_CTLS_IO_EXITING               (1u << 24)
#define PROCBASED_CTLS_MSR_BITMAPS              (1u << 28)
#define PROCBASED_CTLS_PROCBASED_CTLS2          (1u << 31)

/* PINBASED_CTLS flags */
#define PINBASED_CTLS_EXT_INT_EXITING           (1u << 0)
#define PINBASED_CTLS_NMI_EXITING               (1u << 3)

/* EXIT_CTLS flags */
#define EXIT_CTLS_64BIT_MODE                    (1u << 9)
#define EXIT_CTLS_SAVE_IA32_PAT                 (1u << 18)
#define EXIT_CTLS_LOAD_IA32_PAT                 (1u << 19)
#define EXIT_CTLS_SAVE_IA32_EFER                (1u << 20)
#define EXIT_CTLS_LOAD_IA32_EFER                (1u << 21)

/* ENTRY_CTLS flags */
#define ENTRY_CTLS_IA32E_MODE                   (1u << 9)
#define ENTRY_CTLS_LOAD_IA32_PAT                (1u << 14)
#define ENTRY_CTLS_LOAD_IA32_EFER               (1u << 15)

/* LINK_POINTER values */
#define LINK_POINTER_INVALIDATE                 UINT64_MAX

/* GUEST_XX_ACCESS_RIGHTS flags */
#define GUEST_XX_ACCESS_RIGHTS_UNUSABLE         (1u << 16)
// See Volume 3, Section 24.4.1 for access rights format.
#define GUEST_XX_ACCESS_RIGHTS_TYPE_A           (1u << 0)
#define GUEST_XX_ACCESS_RIGHTS_TYPE_W           (1u << 1)
#define GUEST_XX_ACCESS_RIGHTS_TYPE_E           (1u << 2)
#define GUEST_XX_ACCESS_RIGHTS_TYPE_CODE        (1u << 3)
// See Volume 3, Section 3.4.5.1 for valid non-system selector types.
#define GUEST_XX_ACCESS_RIGHTS_S                (1u << 4)
#define GUEST_XX_ACCESS_RIGHTS_P                (1u << 7)
#define GUEST_XX_ACCESS_RIGHTS_L                (1u << 13)
// See Volume 3, Section 3.5 for valid system selectors types.
#define GUEST_TR_ACCESS_RIGHTS_TSS_BUSY         (11u << 0)

/* VMCS fields */
enum class VmcsField16 : uint64_t {
    VPID                            = 0x0000, // Virtual processor ID
    GUEST_CS_SELECTOR               = 0x0802, // Guest CS selector
    GUEST_TR_SELECTOR               = 0x080e, // Guest TR selector
    HOST_ES_SELECTOR                = 0x0c00, // Host ES selector
    HOST_CS_SELECTOR                = 0x0c02, // Host CS selector
    HOST_SS_SELECTOR                = 0x0c04, // Host SS selector
    HOST_DS_SELECTOR                = 0x0c06, // Host DS selector
    HOST_FS_SELECTOR                = 0x0c08, // Host FS selector
    HOST_GS_SELECTOR                = 0x0c0a, // Host GS selector
    HOST_TR_SELECTOR                = 0x0c0c, // Host TR selector
};

enum class VmcsField64 : uint64_t {
    MSR_BITMAPS_ADDRESS             = 0x2004, // Address of MSR bitmaps
    EXIT_MSR_STORE_ADDRESS          = 0x2006, // VM-exit MSR-store address
    EXIT_MSR_LOAD_ADDRESS           = 0x2008, // VM-exit MSR-load address
    ENTRY_MSR_LOAD_ADDRESS          = 0x200a, // VM-entry MSR-load address
    VIRTUAL_APIC_ADDRESS            = 0x2012, // Virtual-APIC address
    APIC_ACCESS_ADDRESS             = 0x2014, // APIC-access address
    EPT_POINTER                     = 0x201a, // EPT pointer
    GUEST_PHYSICAL_ADDRESS          = 0x2400, // Guest physical address
    LINK_POINTER                    = 0x2800, // VMCS link pointer
    GUEST_IA32_PAT                  = 0x2804, // Guest PAT
    GUEST_IA32_EFER                 = 0x2806, // Guest EFER
    HOST_IA32_PAT                   = 0x2c00, // Host PAT
    HOST_IA32_EFER                  = 0x2c02, // Host EFER
};

enum class VmcsField32 : uint64_t {
    PINBASED_CTLS                   = 0x4000, // Pin-based controls
    PROCBASED_CTLS                  = 0x4002, // Primary processor-based controls
    EXCEPTION_BITMAP                = 0x4004, // Exception bitmap
    PAGEFAULT_ERRORCODE_MASK        = 0x4006, // Page-fault error-code mask
    PAGEFAULT_ERRORCODE_MATCH       = 0x4008, // Page-fault error-code match
    EXIT_CTLS                       = 0x400c, // VM-exit controls
    EXIT_MSR_STORE_COUNT            = 0x400e, // VM-exit MSR-store count
    EXIT_MSR_LOAD_COUNT             = 0x4010, // VM-exit MSR-load count
    ENTRY_CTLS                      = 0x4012, // VM-entry controls
    ENTRY_MSR_LOAD_COUNT            = 0x4014, // VM-entry MSR-load count
    ENTRY_INTERRUPTION_INFORMATION  = 0x4016, // VM-entry interruption-information field
    ENTRY_EXCEPTION_ERROR_CODE      = 0x4018, // VM-entry exception error code
    PROCBASED_CTLS2                 = 0x401e, // Secondary processor-based controls
    INSTRUCTION_ERROR               = 0x4400, // VM instruction error
    EXIT_REASON                     = 0x4402, // Exit reason
    EXIT_INTERRUPTION_INFORMATION   = 0x4404, // VM-exit interruption information
    EXIT_INTERRUPTION_ERROR_CODE    = 0x4406, // VM-exit interruption error code
    EXIT_INSTRUCTION_LENGTH         = 0x440c, // VM-exit instruction length
    EXIT_INSTRUCTION_INFORMATION    = 0x440e, // VM-exit instruction information
    HOST_IA32_SYSENTER_CS           = 0x4c00, // Host SYSENTER CS
    GUEST_GDTR_LIMIT                = 0x4810, // Guest GDTR Limit
    GUEST_IDTR_LIMIT                = 0x4812, // Guest IDTR Limit
    GUEST_CS_ACCESS_RIGHTS          = 0x4816, // Guest CS Access Rights
    GUEST_ES_ACCESS_RIGHTS          = 0x4814, // Guest ES Access Rights
    GUEST_SS_ACCESS_RIGHTS          = 0x4818, // Guest SS Access Rights
    GUEST_DS_ACCESS_RIGHTS          = 0x481a, // Guest DS Access Rights
    GUEST_FS_ACCESS_RIGHTS          = 0x481c, // Guest FS Access Rights
    GUEST_GS_ACCESS_RIGHTS          = 0x481e, // Guest GS Access Rights
    GUEST_LDTR_ACCESS_RIGHTS        = 0x4820, // Guest LDTR Access Rights
    GUEST_TR_ACCESS_RIGHTS          = 0x4822, // Guest TR Access Rights
    GUEST_INTERRUPTIBILITY_STATE    = 0x4824, // Guest interruptibility state
    GUEST_ACTIVITY_STATE            = 0x4826, // Guest activity state
    GUEST_IA32_SYSENTER_CS          = 0x482a, // Guest SYSENTER CS
};

enum class VmcsFieldXX : uint64_t {
    CR4_GUEST_HOST_MASK             = 0x6002, // CR4 guest/host mask
    CR4_READ_SHADOW                 = 0x6006, // CR4 read shadow
    EXIT_QUALIFICATION              = 0x6400, // Exit qualification
    GUEST_LINEAR_ADDRESS            = 0x640a, // Guest linear address
    GUEST_CR0                       = 0x6800, // Guest CR0
    GUEST_CR3                       = 0x6802, // Guest CR3
    GUEST_CR4                       = 0x6804, // Guest CR4
    GUEST_GDTR_BASE                 = 0x6816, // Guest GDTR base
    GUEST_IDTR_BASE                 = 0x6818, // Guest IDTR base
    GUEST_RSP                       = 0x681c, // Guest RSP
    GUEST_RIP                       = 0x681e, // Guest RIP
    GUEST_RFLAGS                    = 0x6820, // Guest RFLAGS
    GUEST_PENDING_DEBUG_EXCEPTIONS  = 0x6822, // Guest pending debug exceptions
    GUEST_IA32_SYSENTER_ESP         = 0x6824, // Guest SYSENTER ESP
    GUEST_IA32_SYSENTER_EIP         = 0x6826, // Guest SYSENTER EIP
    HOST_CR0                        = 0x6c00, // Host CR0
    HOST_CR3                        = 0x6c02, // Host CR3
    HOST_CR4                        = 0x6c04, // Host CR4
    HOST_FS_BASE                    = 0x6c06, // Host FS base
    HOST_GS_BASE                    = 0x6c08, // Host GS base
    HOST_TR_BASE                    = 0x6c0a, // Host TR base
    HOST_GDTR_BASE                  = 0x6c0c, // Host GDTR base
    HOST_IDTR_BASE                  = 0x6c0e, // Host IDTR base
    HOST_IA32_SYSENTER_ESP          = 0x6c10, // Host SYSENTER ESP
    HOST_IA32_SYSENTER_EIP          = 0x6c12, // Host SYSENTER EIP
    HOST_RSP                        = 0x6c14, // Host RSP
    HOST_RIP                        = 0x6c16, // Host RIP
};

// clang-format on

class Vcpu;

/* Loads a VMCS within a given scope. */
class AutoVmcs : public StateReloader {
public:
    AutoVmcs(const paddr_t vmcs_address_);
    ~AutoVmcs();

    void Reload() override;
    void InterruptibleReload();
    void InterruptWindowExiting(bool enable);
    void IssueInterrupt(uint32_t vector);

    uint16_t Read(VmcsField16 field) const;
    uint32_t Read(VmcsField32 field) const;
    uint64_t Read(VmcsField64 field) const;
    uint64_t Read(VmcsFieldXX field) const;
    void Write(VmcsField16 field, uint16_t val);
    void Write(VmcsField32 field, uint32_t val);
    void Write(VmcsField64 field, uint64_t val);
    void Write(VmcsFieldXX field, uint64_t val);

    mx_status_t SetControl(VmcsField32 controls, uint64_t true_msr, uint64_t old_msr, uint32_t set,
                           uint32_t clear);

private:
    const paddr_t vmcs_address_;
};

/* Pins execution to a CPU within a given scope. */
class AutoPin {
public:
    AutoPin(const Vcpu* vcpu);
    ~AutoPin();

private:
    thread_t* thread_;
    int prev_cpu_;
};
