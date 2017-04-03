// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define X86_MSR_IA32_FEATURE_CONTROL                0x003a      /* Feature control */
#define X86_MSR_IA32_VMX_BASIC                      0x0480      /* Basic info */
#define X86_MSR_IA32_VMX_PINBASED_CTLS              0x0481      /* Pin-based controls */
#define X86_MSR_IA32_VMX_PROCBASED_CTLS             0x0482      /* Primary processor-based controls */
#define X86_MSR_IA32_VMX_EXIT_CTLS                  0x0483      /* VM-exit controls */
#define X86_MSR_IA32_VMX_ENTRY_CTLS                 0x0484      /* VM-entry controls */
#define X86_MSR_IA32_VMX_CR0_FIXED0                 0x0486      /* CR0 bits that must be 0 to enter VMX */
#define X86_MSR_IA32_VMX_CR0_FIXED1                 0x0487      /* CR0 bits that must be 1 to enter VMX */
#define X86_MSR_IA32_VMX_CR4_FIXED0                 0x0488      /* CR4 bits that must be 0 to enter VMX */
#define X86_MSR_IA32_VMX_CR4_FIXED1                 0x0489      /* CR4 bits that must be 1 to enter VMX */
#define X86_MSR_IA32_VMX_PROCBASED_CTLS2            0x048b      /* Secondary processor-based controls */
#define X86_MSR_IA32_VMX_EPT_VPID_CAP               0x048c      /* VPID and EPT Capabilities */
#define X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS         0x048d      /* True pin-based controls */
#define X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS        0x048e      /* True primary processor-based controls */
#define X86_MSR_IA32_VMX_TRUE_EXIT_CTLS             0x048f      /* True VM-exit controls */
#define X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS            0x0490      /* True VM-entry controls */

/* VMX basic info */
#define VMX_MEMORY_TYPE_WRITE_BACK                  0x06        /* Write back */

/* X86_MSR_IA32_FEATURE_CONTROL flags */
#define X86_MSR_IA32_FEATURE_CONTROL_LOCK           (1u << 0)   /* Locked */
#define X86_MSR_IA32_FEATURE_CONTROL_VMXON          (1u << 2)   /* Enable VMXON */

/* VMCS fields */
#define VMCS_16_VPID                                0x0000      /* Virtual processor ID */
#define VMCS_16_GUEST_CS_SELECTOR                   0x0802      /* Guest CS selector */
#define VMCS_16_GUEST_TR_SELECTOR                   0x080e      /* Guest TR selector */
#define VMCS_16_HOST_ES_SELECTOR                    0x0c00      /* Host ES selector */
#define VMCS_16_HOST_CS_SELECTOR                    0x0c02      /* Host CS selector */
#define VMCS_16_HOST_SS_SELECTOR                    0x0c04      /* Host SS selector */
#define VMCS_16_HOST_DS_SELECTOR                    0x0c06      /* Host DS selector */
#define VMCS_16_HOST_FS_SELECTOR                    0x0c08      /* Host FS selector */
#define VMCS_16_HOST_GS_SELECTOR                    0x0c0a      /* Host GS selector */
#define VMCS_16_HOST_TR_SELECTOR                    0x0c0c      /* Host TR selector */

#define VMCS_64_EPT_POINTER                         0x201a      /* EPT pointer */
#define VMCS_64_GUEST_PHYSICAL_ADDRESS              0x2400      /* Guest physical address */
#define VMCS_64_LINK_POINTER                        0x2800      /* VMCS link pointer */
#define VMCS_64_GUEST_IA32_PAT                      0x2804      /* Guest PAT */
#define VMCS_64_GUEST_IA32_EFER                     0x2806      /* Guest EFER */
#define VMCS_64_HOST_IA32_PAT                       0x2c00      /* Host PAT */
#define VMCS_64_HOST_IA32_EFER                      0x2c02      /* Host EFER */

#define VMCS_32_PINBASED_CTLS                       0x4000      /* Pin-based controls */
#define VMCS_32_PROCBASED_CTLS                      0x4002      /* Primary processor-based controls */
#define VMCS_32_EXCEPTION_BITMAP                    0x4004      /* Exception bitmap */
#define VMCS_32_PAGEFAULT_ERRORCODE_MASK            0x4006      /* Page-fault error-code mask */
#define VMCS_32_PAGEFAULT_ERRORCODE_MATCH           0x4008      /* Page-fault error-code match */
#define VMCS_32_EXIT_CTLS                           0x400c      /* VM-exit controls */
#define VMCS_32_ENTRY_CTLS                          0x4012      /* VM-entry controls */
#define VMCS_32_PROCBASED_CTLS2                     0x401e      /* Secondary processor-based controls */
#define VMCS_32_INSTRUCTION_ERROR                   0x4400      /* Instruction error */
#define VMCS_32_EXIT_REASON                         0x4402      /* Exit reason */
#define VMCS_32_INTERRUPTION_INFORMATION            0x4404      /* VM-exit interruption information */
#define VMCS_32_INTERRUPTION_ERROR_CODE             0x4406      /* VM-exit interruption error code */
#define VMCS_32_INSTRUCTION_LENGTH                  0x440c      /* VM-exit instruction length */
#define VMCS_32_INSTRUCTION_INFORMATION             0x440e      /* VM-exit instruction information */
#define VMCS_32_HOST_IA32_SYSENTER_CS               0x4c00      /* Host SYSENTER CS */
#define VMCS_32_GUEST_GDTR_LIMIT                    0x4810      /* Guest GDTR Limit */
#define VMCS_32_GUEST_IDTR_LIMIT                    0x4812      /* Guest IDTR Limit */
#define VMCS_32_GUEST_CS_ACCESS_RIGHTS              0x4816      /* Guest CS Access Rights */
#define VMCS_32_GUEST_ES_ACCESS_RIGHTS              0x4814      /* Guest ES Access Rights */
#define VMCS_32_GUEST_SS_ACCESS_RIGHTS              0x4818      /* Guest SS Access Rights */
#define VMCS_32_GUEST_DS_ACCESS_RIGHTS              0x481a      /* Guest DS Access Rights */
#define VMCS_32_GUEST_FS_ACCESS_RIGHTS              0x481c      /* Guest FS Access Rights */
#define VMCS_32_GUEST_GS_ACCESS_RIGHTS              0x481e      /* Guest GS Access Rights */
#define VMCS_32_GUEST_LDTR_ACCESS_RIGHTS            0x4820      /* Guest LDTR Access Rights */
#define VMCS_32_GUEST_TR_ACCESS_RIGHTS              0x4822      /* Guest TR Access Rights */
#define VMCS_32_GUEST_INTERRUPTIBILITY_STATE        0x4824      /* Guest interruptibility state */
#define VMCS_32_GUEST_ACTIVITY_STATE                0x4826      /* Guest activity state */
#define VMCS_32_GUEST_IA32_SYSENTER_CS              0x482a      /* Guest SYSENTER CS */

#define VMCS_XX_EXIT_QUALIFICATION                  0x6400      /* Exit qualification */
#define VMCS_XX_GUEST_LINEAR_ADDRESS                0x640a      /* Guest linear address */
#define VMCS_XX_GUEST_CR0                           0x6800      /* Guest CR0 */
#define VMCS_XX_GUEST_CR3                           0x6802      /* Guest CR3 */
#define VMCS_XX_GUEST_CR4                           0x6804      /* Guest CR4 */
#define VMCS_XX_GUEST_GDTR_BASE                     0x6816      /* Guest GDTR base */
#define VMCS_XX_GUEST_IDTR_BASE                     0x6818      /* Guest IDTR base */
#define VMCS_XX_GUEST_RSP                           0x681c      /* Guest RSP */
#define VMCS_XX_GUEST_RIP                           0x681e      /* Guest RIP */
#define VMCS_XX_GUEST_RFLAGS                        0x6820      /* Guest RFLAGS */
#define VMCS_XX_GUEST_PENDING_DEBUG_EXCEPTIONS      0x6822      /* Guest pending debug exceptions */
#define VMCS_XX_GUEST_IA32_SYSENTER_ESP             0x6824      /* Guest SYSENTER ESP */
#define VMCS_XX_GUEST_IA32_SYSENTER_EIP             0x6826      /* Guest SYSENTER EIP */
#define VMCS_XX_HOST_CR0                            0x6c00      /* Host CR0 */
#define VMCS_XX_HOST_CR3                            0x6c02      /* Host CR3 */
#define VMCS_XX_HOST_CR4                            0x6c04      /* Host CR4 */
#define VMCS_XX_HOST_FS_BASE                        0x6c06      /* Host FS base */
#define VMCS_XX_HOST_GS_BASE                        0x6c08      /* Host GS base */
#define VMCS_XX_HOST_TR_BASE                        0x6c0a      /* Host TR base */
#define VMCS_XX_HOST_GDTR_BASE                      0x6c0c      /* Host GDTR base */
#define VMCS_XX_HOST_IDTR_BASE                      0x6c0e      /* Host IDTR base */
#define VMCS_XX_HOST_IA32_SYSENTER_ESP              0x6c10      /* Host SYSENTER ESP */
#define VMCS_XX_HOST_IA32_SYSENTER_EIP              0x6c12      /* Host SYSENTER EIP */
#define VMCS_XX_HOST_RSP                            0x6c14      /* Host RSP */
#define VMCS_XX_HOST_RIP                            0x6c16      /* Host RIP */

/* VMCS_32_PROCBASED_CTLS2 flags */
#define VMCS_32_PROCBASED_CTLS2_EPT                 (1u << 1)
#define VMCS_32_PROCBASED_CTLS2_RDTSCP              (1u << 3)
#define VMCS_32_PROCBASED_CTLS2_VPID                (1u << 5)
#define VMCS_32_PROCBASED_CTLS2_XSAVES_XRSTORS      (1u << 20)

/* VMCS_32_PROCBASED_CTLS flags */
#define VMCS_32_PROCBASED_CTLS_IO_EXITING           (1u << 24)
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
#define VMCS_32_EXIT_REASON_EXTERNAL_INTERRUPT      1u
#define VMCS_32_EXIT_REASON_IO_INSTRUCTION          30u

/* VMCS_32_GUEST_XX_ACCESS_RIGHTS flags */
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE     (1u << 16)
// See Volume 3, Section 24.4.1 for access rights format.
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_A       (1u << 0)
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_W       (1u << 1)
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_E       (1u << 2)
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_CODE    (1u << 3)
// See Volume 3, Section 3.4.5.1 for valid non-system selector types.
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_S            (1u << 4)
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_P            (1u << 7)
#define VMCS_32_GUEST_XX_ACCESS_RIGHTS_L            (1u << 13)
// See Volume 3, Section 3.5 for valid system selectors types.
#define VMCS_32_GUEST_TR_ACCESS_RIGHTS_TSS_BUSY     (11u << 0)


/* Stores VMX info from the VMX basic MSR. */
struct VmxInfo {
    uint32_t revision_id;
    uint16_t region_size;
    bool write_back;
    bool io_exit_info;
    bool vmx_controls;

    VmxInfo();
};

struct EptInfo {
    bool page_walk_4;
    bool write_back;
    bool pde_2mb_page;
    bool pdpe_1gb_page;
    bool ept_flags;
    bool exit_info;
    bool invept;

    EptInfo();
};

/* VMX region to be used with both VMXON and VMCS. */
struct VmxRegion {
    uint32_t revision_id;
};

/* Base class for CPU contexts. */
class PerCpu {
public:
    virtual status_t Init(const VmxInfo& vmx_info);

protected:
    VmxPage page_;

    virtual ~PerCpu() {}
};

/* Creates a VMXON CPU context to initialize VMX. */
class VmxonPerCpu : public PerCpu {
public:
    status_t VmxOn();
    status_t VmxOff();

private:
    bool is_on_ = false;
};

struct AutoVmcsLoad {
    AutoVmcsLoad(VmxPage* page);
    ~AutoVmcsLoad();
};

/* Creates a VMCS CPU context to initialize a VM. */
class VmcsPerCpu : public PerCpu {
public:
    status_t Clear();
    status_t Setup(paddr_t pml4_address);
    status_t Enter(const VmcsContext& context);

private:
    bool do_resume_ = false;
    VmxState vmx_state_;
};

template<typename T>
status_t InitPerCpus(const VmxInfo& vmx_info, mxtl::Array<T>* ctxs) {
    for (size_t i = 0; i < ctxs->size(); i++) {
        status_t status = (*ctxs)[i].Init(vmx_info);
        if (status != NO_ERROR)
            return status;
    }
    return NO_ERROR;
}
