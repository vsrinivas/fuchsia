// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <hypervisor/state_invalidator.h>

// clang-format off

#define X86_MSR_IA32_VMX_PINBASED_CTLS                  0x0481
#define X86_MSR_IA32_VMX_PROCBASED_CTLS                 0x0482
#define X86_MSR_IA32_VMX_EXIT_CTLS                      0x0483
#define X86_MSR_IA32_VMX_ENTRY_CTLS                     0x0484
#define X86_MSR_IA32_VMX_PROCBASED_CTLS2                0x048b
#define X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS             0x048d
#define X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS            0x048e
#define X86_MSR_IA32_VMX_TRUE_EXIT_CTLS                 0x048f
#define X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS                0x0490

// PROCBASED_CTLS2 flags.
static const uint32_t kProcbasedCtls2Ept                = 1u << 1;
static const uint32_t kProcbasedCtls2Rdtscp             = 1u << 3;
static const uint32_t kProcbasedCtls2x2Apic             = 1u << 4;
static const uint32_t kProcbasedCtls2Vpid               = 1u << 5;
static const uint32_t kProcbasedCtls2UnrestrictedGuest  = 1u << 7;
static const uint32_t kProcbasedCtls2Invpcid            = 1u << 12;

// PROCBASED_CTLS flags.
static const uint32_t kProcbasedCtlsIntWindowExiting    = 1u << 2;
static const uint32_t kProcbasedCtlsHltExiting          = 1u << 7;
static const uint32_t kProcbasedCtlsCr3LoadExiting      = 1u << 15;
static const uint32_t kProcbasedCtlsCr3StoreExiting     = 1u << 16;
static const uint32_t kProcbasedCtlsCr8LoadExiting      = 1u << 19;
static const uint32_t kProcbasedCtlsCr8StoreExiting     = 1u << 20;
static const uint32_t kProcbasedCtlsTprShadow           = 1u << 21;
static const uint32_t kProcbasedCtlsIoExiting           = 1u << 24;
static const uint32_t kProcbasedCtlsMsrBitmaps          = 1u << 28;
static const uint32_t kProcbasedCtlsPauseExiting        = 1u << 30;
static const uint32_t kProcbasedCtlsProcbasedCtls2      = 1u << 31;

// PINBASED_CTLS flags.
static const uint32_t kPinbasedCtlsExtIntExiting        = 1u << 0;
static const uint32_t kPinbasedCtlsNmiExiting           = 1u << 3;

// EXIT_CTLS flags.
static const uint32_t kExitCtls64bitMode                = 1u << 9;
static const uint32_t kExitCtlsAckIntOnExit             = 1u << 15;
static const uint32_t kExitCtlsSaveIa32Pat              = 1u << 18;
static const uint32_t kExitCtlsLoadIa32Pat              = 1u << 19;
static const uint32_t kExitCtlsSaveIa32Efer             = 1u << 20;
static const uint32_t kExitCtlsLoadIa32Efer             = 1u << 21;

// ENTRY_CTLS flags.
static const uint32_t kEntryCtlsIa32eMode               = 1u << 9;
static const uint32_t kEntryCtlsLoadIa32Pat             = 1u << 14;
static const uint32_t kEntryCtlsLoadIa32Efer            = 1u << 15;

// LINK_POINTER values.
static const uint64_t kLinkPointerInvalidate            = UINT64_MAX;

// GUEST_XX_ACCESS_RIGHTS flags.
static const uint32_t kGuestXxAccessRightsUnusable      = 1u << 16;
// See Volume 3, Section 24.4.1 for access rights format.
static const uint32_t kGuestXxAccessRightsTypeA         = 1u << 0;
static const uint32_t kGuestXxAccessRightsTypeW         = 1u << 1;
static const uint32_t kGuestXxAccessRightsTypeE         = 1u << 2;
static const uint32_t kGuestXxAccessRightsTypeCode      = 1u << 3;
// See Volume 3, Section 3.4.5.1 for valid non-system selector types.
static const uint32_t kGuestXxAccessRightsS             = 1u << 4;
static const uint32_t kGuestXxAccessRightsP             = 1u << 7;
static const uint32_t kGuestXxAccessRightsL             = 1u << 13;
// See Volume 3, Section 3.5 for valid system selectors types.
static const uint32_t kGuestTrAccessRightsTssBusy16Bit  = 3u << 0;
static const uint32_t kGuestTrAccessRightsTssBusy       = 11u << 0;

static const uint32_t kGuestXxAccessRightsDefault       = kGuestXxAccessRightsTypeA |
                                                          kGuestXxAccessRightsTypeW |
                                                          kGuestXxAccessRightsS |
                                                          kGuestXxAccessRightsP;

// GUEST_INTERRUPTIBILITY_STATE flags.
static const uint32_t kInterruptibilityStiBlocking      = 1u << 0;
static const uint32_t kInterruptibilityMovSsBlocking    = 1u << 1;

// VMCS fields.
enum class VmcsField16 : uint64_t {
    VPID                                                = 0x0000,
    GUEST_CS_SELECTOR                                   = 0x0802,
    GUEST_TR_SELECTOR                                   = 0x080e,
    HOST_ES_SELECTOR                                    = 0x0c00,
    HOST_CS_SELECTOR                                    = 0x0c02,
    HOST_SS_SELECTOR                                    = 0x0c04,
    HOST_DS_SELECTOR                                    = 0x0c06,
    HOST_FS_SELECTOR                                    = 0x0c08,
    HOST_GS_SELECTOR                                    = 0x0c0a,
    HOST_TR_SELECTOR                                    = 0x0c0c,
};

enum class VmcsField64 : uint64_t {
    MSR_BITMAPS_ADDRESS                                 = 0x2004,
    EXIT_MSR_STORE_ADDRESS                              = 0x2006,
    EXIT_MSR_LOAD_ADDRESS                               = 0x2008,
    ENTRY_MSR_LOAD_ADDRESS                              = 0x200a,
    EPT_POINTER                                         = 0x201a,
    GUEST_PHYSICAL_ADDRESS                              = 0x2400,
    LINK_POINTER                                        = 0x2800,
    GUEST_IA32_PAT                                      = 0x2804,
    GUEST_IA32_EFER                                     = 0x2806,
    HOST_IA32_PAT                                       = 0x2c00,
    HOST_IA32_EFER                                      = 0x2c02,
};

enum class VmcsField32 : uint64_t {
    PINBASED_CTLS                                       = 0x4000,
    PROCBASED_CTLS                                      = 0x4002,
    EXCEPTION_BITMAP                                    = 0x4004,
    PAGEFAULT_ERRORCODE_MASK                            = 0x4006,
    PAGEFAULT_ERRORCODE_MATCH                           = 0x4008,
    EXIT_CTLS                                           = 0x400c,
    EXIT_MSR_STORE_COUNT                                = 0x400e,
    EXIT_MSR_LOAD_COUNT                                 = 0x4010,
    ENTRY_CTLS                                          = 0x4012,
    ENTRY_MSR_LOAD_COUNT                                = 0x4014,
    ENTRY_INTERRUPTION_INFORMATION                      = 0x4016,
    ENTRY_EXCEPTION_ERROR_CODE                          = 0x4018,
    PROCBASED_CTLS2                                     = 0x401e,
    INSTRUCTION_ERROR                                   = 0x4400,
    EXIT_REASON                                         = 0x4402,
    EXIT_INTERRUPTION_INFORMATION                       = 0x4404,
    EXIT_INTERRUPTION_ERROR_CODE                        = 0x4406,
    EXIT_INSTRUCTION_LENGTH                             = 0x440c,
    EXIT_INSTRUCTION_INFORMATION                        = 0x440e,
    HOST_IA32_SYSENTER_CS                               = 0x4c00,

    GUEST_ES_LIMIT                                      = 0x4800,
    GUEST_CS_LIMIT                                      = 0x4802,
    GUEST_SS_LIMIT                                      = 0x4804,
    GUEST_DS_LIMIT                                      = 0x4806,
    GUEST_FS_LIMIT                                      = 0x4808,
    GUEST_GS_LIMIT                                      = 0x480a,
    GUEST_LDTR_LIMIT                                    = 0x480c,
    GUEST_TR_LIMIT                                      = 0x480e,

    GUEST_GDTR_LIMIT                                    = 0x4810,
    GUEST_IDTR_LIMIT                                    = 0x4812,
    GUEST_CS_ACCESS_RIGHTS                              = 0x4816,
    GUEST_ES_ACCESS_RIGHTS                              = 0x4814,
    GUEST_SS_ACCESS_RIGHTS                              = 0x4818,
    GUEST_DS_ACCESS_RIGHTS                              = 0x481a,
    GUEST_FS_ACCESS_RIGHTS                              = 0x481c,
    GUEST_GS_ACCESS_RIGHTS                              = 0x481e,
    GUEST_LDTR_ACCESS_RIGHTS                            = 0x4820,
    GUEST_TR_ACCESS_RIGHTS                              = 0x4822,
    GUEST_INTERRUPTIBILITY_STATE                        = 0x4824,
    GUEST_ACTIVITY_STATE                                = 0x4826,
    GUEST_IA32_SYSENTER_CS                              = 0x482a,
};

enum class VmcsFieldXX : uint64_t {
    CR0_GUEST_HOST_MASK                                 = 0x6000,
    CR4_GUEST_HOST_MASK                                 = 0x6002,
    CR0_READ_SHADOW                                     = 0x6004,
    CR4_READ_SHADOW                                     = 0x6006,
    EXIT_QUALIFICATION                                  = 0x6400,
    GUEST_LINEAR_ADDRESS                                = 0x640a,
    GUEST_CR0                                           = 0x6800,
    GUEST_CR3                                           = 0x6802,
    GUEST_CR4                                           = 0x6804,

    GUEST_ES_BASE                                       = 0x6806,
    GUEST_CS_BASE                                       = 0x6808,
    GUEST_SS_BASE                                       = 0x680A,
    GUEST_DS_BASE                                       = 0x680C,
    GUEST_FS_BASE                                       = 0x680E,
    GUEST_GS_BASE                                       = 0x6810,
    GUEST_TR_BASE                                       = 0x6814,

    GUEST_GDTR_BASE                                     = 0x6816,
    GUEST_IDTR_BASE                                     = 0x6818,
    GUEST_RSP                                           = 0x681c,
    GUEST_RIP                                           = 0x681e,
    GUEST_RFLAGS                                        = 0x6820,
    GUEST_PENDING_DEBUG_EXCEPTIONS                      = 0x6822,
    GUEST_IA32_SYSENTER_ESP                             = 0x6824,
    GUEST_IA32_SYSENTER_EIP                             = 0x6826,
    HOST_CR0                                            = 0x6c00,
    HOST_CR3                                            = 0x6c02,
    HOST_CR4                                            = 0x6c04,
    HOST_FS_BASE                                        = 0x6c06,
    HOST_GS_BASE                                        = 0x6c08,
    HOST_TR_BASE                                        = 0x6c0a,
    HOST_GDTR_BASE                                      = 0x6c0c,
    HOST_IDTR_BASE                                      = 0x6c0e,
    HOST_IA32_SYSENTER_ESP                              = 0x6c10,
    HOST_IA32_SYSENTER_EIP                              = 0x6c12,
    HOST_RSP                                            = 0x6c14,
    HOST_RIP                                            = 0x6c16,
};

// INVEPT invalidation types.
enum class InvEpt : uint64_t {
    SINGLE_CONTEXT                                      = 1,
    ALL_CONTEXT                                         = 2,
};

// clang-format on

// Loads a VMCS within a given scope.
class AutoVmcs : public hypervisor::StateInvalidator {
public:
    AutoVmcs(paddr_t vmcs_address_);
    ~AutoVmcs();

    void Invalidate() override;
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

    zx_status_t SetControl(VmcsField32 controls, uint64_t true_msr, uint64_t old_msr, uint32_t set,
                           uint32_t clear);

private:
    paddr_t vmcs_address_;
};

// Pins execution to a CPU within a given scope.
class AutoPin {
public:
    AutoPin(uint16_t vpid);
    ~AutoPin();

private:
    cpu_mask_t prev_cpu_mask_;
    thread_t* thread_;
};

bool cr0_is_invalid(AutoVmcs* vmcs, uint64_t cr0_value);
