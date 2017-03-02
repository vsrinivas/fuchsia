// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define X86_MSR_IA32_FEATURE_CONTROL                0x003a      /* Feature control */
#define X86_MSR_IA32_VMX_BASIC                      0x0480      /* Basic info */
#define X86_MSR_IA32_VMX_CR0_FIXED0                 0x0486      /* CR0 bits that must be 0 to enter VMX */
#define X86_MSR_IA32_VMX_CR0_FIXED1                 0x0487      /* CR0 bits that must be 1 to enter VMX */
#define X86_MSR_IA32_VMX_CR4_FIXED0                 0x0488      /* CR4 bits that must be 0 to enter VMX */
#define X86_MSR_IA32_VMX_CR4_FIXED1                 0x0489      /* CR4 bits that must be 1 to enter VMX */
#define X86_MSR_IA32_VMX_PROCBASED_CTLS2            0x048b      /* Secondary processor-based controls */
#define X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS         0x048d      /* Pin-based controls */
#define X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS        0x048e      /* Primary processor-based controls */
#define X86_MSR_IA32_VMX_TRUE_EXIT_CTLS             0x048f      /* VM-exit controls */
#define X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS            0x0490      /* VM-entry controls */

/* VMX basic info */
#define VMX_MEMORY_TYPE_WRITE_BACK                  0x06        /* Write back */

/* X86_MSR_IA32_FEATURE_CONTROL flags */
#define X86_MSR_IA32_FEATURE_CONTROL_LOCK           (1u << 0)   /* Locked */
#define X86_MSR_IA32_FEATURE_CONTROL_VMXON          (1u << 2)   /* Enable VMXON */

/* VMCS fields */
#define VMCS_16_VPID                                0x0000      /* Virtual processor ID */
#define VMCS_16_GUEST_CS_SELECTOR                   0x0802      /* Guest CS selector */
#define VMCS_16_HOST_CS_SELECTOR                    0x0c02      /* Host CS selector */
#define VMCS_16_HOST_TR_SELECTOR                    0x0c0c      /* Host TR selector */
#define VMCS_32_PINBASED_CTLS                       0x4000      /* Pin-based controls */
#define VMCS_32_PROCBASED_CTLS                      0x4002      /* Primary processor-based controls */
#define VMCS_32_EXCEPTION_BITMAP                    0x4004      /* Exception bitmap */
#define VMCS_32_EXIT_CTLS                           0x400c      /* VM-exit controls */
#define VMCS_32_ENTRY_CTLS                          0x4012      /* VM-entry controls */
#define VMCS_32_PROCBASED_CTLS2                     0x401e      /* Secondary processor-based controls */
#define VMCS_32_INSTRUCTION_ERROR                   0x4400      /* Instruction error */
#define VMCS_32_EXIT_REASON                         0x4402      /* Exit reason */
#define VMCS_64_MSR_BITMAPS_ADDRESS                 0x2004      /* MSR bitmaps address */
#define VMCS_64_LINK_POINTER                        0x2800      /* VMCS link pointer */
#define VMCS_64_HOST_IA32_PAT                       0x2c00      /* Host PAT */
#define VMCS_64_HOST_IA32_EFER                      0x2c02      /* Host EFER */
#define VMCS_XX_GUEST_CR3                           0x6802      /* Guest CR3 */
#define VMCS_XX_GUEST_GDTR_BASE                     0x6816      /* Guest GDTR base */
#define VMCS_XX_GUEST_RSP                           0x681c      /* Guest RSP */
#define VMCS_XX_GUEST_RIP                           0x681e      /* Guest RIP */
#define VMCS_XX_HOST_CR0                            0x6c00      /* Host CR0 */
#define VMCS_XX_HOST_CR3                            0x6c02      /* Host CR3 */
#define VMCS_XX_HOST_CR4                            0x6c04      /* Host CR4 */
#define VMCS_XX_HOST_FS_BASE                        0x6c06      /* Host FS base */
#define VMCS_XX_HOST_GS_BASE                        0x6c08      /* Host GS base */
#define VMCS_XX_HOST_TR_BASE                        0x6c0a      /* Host TR base */
#define VMCS_XX_HOST_GDTR_BASE                      0x6c0c      /* Host GDTR base */
#define VMCS_XX_HOST_IDTR_BASE                      0x6c0e      /* Host IDTR base */
#define VMCS_XX_HOST_RSP                            0x6c14      /* Host RSP */
#define VMCS_XX_HOST_RIP                            0x6c16      /* Host RIP */

/* VMCS_32_PROCBASED_CTLS2 flags */
#define VMCS_32_PROCBASED_CTLS2_EPT                 (1u << 1)
#define VMCS_32_PROCBASED_CTLS2_RDTSCP              (1u << 3)
#define VMCS_32_PROCBASED_CTLS2_VPID                (1u << 5)
#define VMCS_32_PROCBASED_CTLS2_XSAVES_XRSTORS      (1u << 20)

/* VMCS_32_PROCBASED_CTLS flags */
#define VMCS_32_PROCBASED_CTLS_MSR_BITMAPS          (1u << 28)
#define VMCS_32_PROCBASED_CTLS_PROCBASED_CTLS2      (1u << 31)

/* VMCS_32_PINBASED_CTLS flags */
#define VMCS_32_PINBASED_CTLS_EXTINT_EXITING        (1u << 0)
#define VMCS_32_PINBASED_CTLS_NMI_EXITING           (1u << 3)

/* VMCS_32_EXIT_CTLS flags */
#define VMCS_32_EXIT_CTLS_64BIT_MODE                (1u << 9)
#define VMCS_32_EXIT_CTLS_ACK_INTERRUPT             (1u << 15)
#define VMCS_32_EXIT_CTLS_SAVE_IA32_PAT             (1u << 18)
#define VMCS_32_EXIT_CTLS_LOAD_IA32_PAT             (1u << 19)
#define VMCS_32_EXIT_CTLS_SAVE_IA32_EFER            (1u << 20)
#define VMCS_32_EXIT_CTLS_LOAD_IA32_EFER            (1u << 21)

/* VMCS_32_ENTRY_CTLS flags */
#define VMCS_32_ENTRY_CTLS_IA32E_MODE               (1u << 9)
#define VMCS_32_ENTRY_CTLS_LOAD_IA32_PAT            (1u << 14)
#define VMCS_32_ENTRY_CTLS_LOAD_IA32_EFER           (1u << 15)

/* VMCS_32_EXCEPTION_BITMAP values */
#define VMCS_32_EXCEPTION_BITMAP_ALL_EXCEPTIONS     0xffffffff

/* VMCS_64_LINK_POINTER values */
#define VMCS_64_LINK_POINTER_INVALIDATE             0xffffffffffffffff

/* VMCS_32_EXIT_REASON values */
#define VMCS_32_EXIT_REASON_BASIC_MASK              0xffff

/* Stores VMX info from the VMX basic MSR. */
struct VmxInfo {
    uint32_t revision_id;
    uint16_t region_size;
    uint8_t memory_type;
    bool ins_outs;
    bool vmx_controls;

    VmxInfo();
};

/* VMX region to be used with both VMXON and VMCS. */
struct VmxRegion {
    uint32_t revision_id;
};

/* Base class for CPU contexts. */
class VmxCpuContext {
public:
    virtual mx_status_t Init(const VmxInfo& info);

protected:
    VmxPage page_;

    virtual ~VmxCpuContext() {}
};

/* Creates a VMXON CPU context to initialize VMX. */
class VmxonCpuContext : public VmxCpuContext {
public:
    mx_status_t VmxOn();
    mx_status_t VmxOff();
};

/* Creates a VMCS CPU context to initialize a VM. */
class VmcsCpuContext : public VmxCpuContext {
public:
    mx_status_t Init(const VmxInfo& info) override;
    mx_status_t Setup();
    mx_status_t Clear();

private:
    VmxPage msr_bitmaps_page_;
};

template<typename T>
mx_status_t InitCpuContexts(mxtl::Array<T>* ctxs) {
    VmxInfo info;
    for (size_t i = 0; i < ctxs->size(); i++) {
        mx_status_t status = (*ctxs)[i].Init(info);
        if (status != NO_ERROR)
            return status;
    }
    return NO_ERROR;
}

/* Holds the register state used to restore a host. */
struct VmxHostState {
    // Callee-save registers.
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    // Return address.
    uint64_t rip;
};

__BEGIN_CDECLS
/* Save the host state.
 * This is the VMX equivalent of setjmp. If we return 0 we have saved the host
 * state, if we return 1 we have loaded the host state.
 */
int vmx_host_save(VmxHostState* host_state);

/* Load the host state.
 * This is the VMX equivalent of longjmp. This is never called directly by the
 * code, but is executed by VMX on VM exit.
 */
void vmx_host_load();
__END_CDECLS
