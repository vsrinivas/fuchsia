// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <inttypes.h>

#include <bits.h>
#include <new.h>
#include <string.h>

#include <arch/defines.h>
#include <arch/hypervisor.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/hypervisor.h>
#include <arch/x86/hypervisor_state.h>
#include <arch/x86/idt.h>
#include <arch/x86/registers.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <magenta/errors.h>
#include <magenta/fifo_dispatcher.h>

#include "hypervisor_priv.h"

#define VMX_ERR_CHECK(var) \
    "setna %[" #var "];"     // Check CF and ZF for error.

extern uint8_t _gdt[];

static const int kUartIoPort = 0x3f8;

static status_t vmxon(paddr_t pa) {
    uint8_t err;

    __asm__ volatile (
        "vmxon %[pa];"
        VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static status_t vmxoff() {
    uint8_t err;

    __asm__ volatile (
        "vmxoff;"
        VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        :
        : "cc");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static status_t vmptrld(paddr_t pa) {
    uint8_t err;

    __asm__ volatile (
        "vmptrld %[pa];"
        VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static status_t vmclear(paddr_t pa) {
    uint8_t err;

    __asm__ volatile (
        "vmclear %[pa];"
        VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static uint64_t vmread(uint64_t field) {
    uint8_t err;
    uint64_t val;

    __asm__ volatile(
        "vmread %[field], %[val];"
        VMX_ERR_CHECK(err)
        : [err] "=r"(err), [val] "=m"(val)
        : [field] "r"(field)
        : "cc");

    DEBUG_ASSERT(err == NO_ERROR);
    return val;
}

static void vmwrite(uint64_t field, uint64_t val) {
    uint8_t err;

    __asm__ volatile (
        "vmwrite %[val], %[field];"
        VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [val] "r"(val), [field] "r"(field)
        : "cc");

    DEBUG_ASSERT(err == NO_ERROR);
}

// TODO(abdulla): Update this to execute on every CPU. For development, it is
// convenient to only consider a single CPU for now.
static status_t percpu_exec(thread_start_routine entry, void* arg) {
    thread_t *t = thread_create("vmx", entry, arg, HIGH_PRIORITY, DEFAULT_STACK_SIZE);
    if (!t)
        return ERR_NO_MEMORY;

    thread_set_pinned_cpu(t, 0);
    status_t status = thread_resume(t);
    if (status != NO_ERROR)
        return status;

    int retcode;
    status = thread_join(t, &retcode, INFINITE_TIME);
    return status != NO_ERROR ? status : retcode;
}

VmxInfo::VmxInfo() {
    // From Volume 3, Appendix A.1.
    uint64_t basic_info = read_msr(X86_MSR_IA32_VMX_BASIC);
    revision_id = static_cast<uint32_t>(BITS(basic_info, 30, 0));
    region_size = static_cast<uint16_t>(BITS_SHIFT(basic_info, 44, 32));
    write_back = BITS_SHIFT(basic_info, 53, 50) == VMX_MEMORY_TYPE_WRITE_BACK;
    io_exit_info = BIT_SHIFT(basic_info, 54);
    vmx_controls = BIT_SHIFT(basic_info, 55);
}

EptInfo::EptInfo() {
    // From Volume 3, Appendix A.10.
    uint64_t ept_info = read_msr(X86_MSR_IA32_VMX_EPT_VPID_CAP);
    page_walk_4 = BIT_SHIFT(ept_info, 6);
    write_back = BIT_SHIFT(ept_info, 14);
    pde_2mb_page = BIT_SHIFT(ept_info, 16);
    pdpe_1gb_page = BIT_SHIFT(ept_info, 17);
    ept_flags = BIT_SHIFT(ept_info, 21);
    exit_info = BIT_SHIFT(ept_info, 22);
    invept =
        // INVEPT instruction is supported.
        BIT_SHIFT(ept_info, 20) &&
        // Single-context INVEPT type is supported.
        BIT_SHIFT(ept_info, 25) &&
        // All-context INVEPT type is supported.
        BIT_SHIFT(ept_info, 26);
}

VmxPage::~VmxPage() {
    vm_page_t* page = paddr_to_vm_page(pa_);
    if (page != nullptr)
        pmm_free_page(page);
}

status_t VmxPage::Alloc(const VmxInfo& vmx_info) {
    // From Volume 3, Appendix A.1: Bits 44:32 report the number of bytes that
    // software should allocate for the VMXON region and any VMCS region. It is
    // a value greater than 0 and at most 4096 (bit 44 is set if and only if
    // bits 43:32 are clear).
    if (vmx_info.region_size > PAGE_SIZE)
        return ERR_NOT_SUPPORTED;

    // Check use write-back memory for VMX regions is supported.
    if (!vmx_info.write_back)
        return ERR_NOT_SUPPORTED;

    // The maximum size for a VMXON or VMCS region is 4096, therefore
    // unconditionally allocating a page is adequate.
    if (pmm_alloc_page(0, &pa_) == nullptr)
        return ERR_NO_MEMORY;

    DEBUG_ASSERT(IS_PAGE_ALIGNED(pa_));
    memset(VirtualAddress(), 0, PAGE_SIZE);
    return NO_ERROR;
}

paddr_t VmxPage::PhysicalAddress() {
    DEBUG_ASSERT(pa_ != 0);
    return pa_;
}

void* VmxPage::VirtualAddress() {
    DEBUG_ASSERT(pa_ != 0);
    return paddr_to_kvaddr(pa_);
}

static bool cr_is_invalid(uint64_t cr_value, uint32_t fixed0_msr, uint32_t fixed1_msr) {
    uint64_t fixed0 = read_msr(fixed0_msr);
    uint64_t fixed1 = read_msr(fixed1_msr);
    return ~(cr_value | ~fixed0) != 0 || ~(~cr_value | fixed1) != 0;
}

static int vmx_enable(void* arg) {
    VmxonContext* context = static_cast<VmxonContext*>(arg);
    VmxonPerCpu* per_cpu = context->PerCpu();

    // Check that we have instruction information when we VM exit on IO.
    VmxInfo vmx_info;
    if (!vmx_info.io_exit_info)
        return ERR_NOT_SUPPORTED;

    // Check that full VMX controls are supported.
    if (!vmx_info.vmx_controls)
        return ERR_NOT_SUPPORTED;

    // Check that a page-walk length of 4 is supported.
    EptInfo ept_info;
    if (!ept_info.page_walk_4)
        return ERR_NOT_SUPPORTED;

    // Check use write-back memory for EPT is supported.
    if (!ept_info.write_back)
        return ERR_NOT_SUPPORTED;

    // Check that accessed and dirty flags for EPT are supported.
    if (!ept_info.ept_flags)
        return ERR_NOT_SUPPORTED;

    // Check that the INVEPT instruction is supported.
    if (!ept_info.invept)
        return ERR_NOT_SUPPORTED;

    // Enable VMXON, if required.
    uint64_t feature_control = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
    if (!(feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) ||
        !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON)) {
        if ((feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) &&
            !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON)) {
            return ERR_NOT_SUPPORTED;
        }
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_LOCK;
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_VMXON;
        write_msr(X86_MSR_IA32_FEATURE_CONTROL, feature_control);
    }


    // Check control registers are in a VMX-friendly state.
    uint64_t cr0 = x86_get_cr0();
    if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1)) {
        return ERR_BAD_STATE;
    }
    uint64_t cr4 = x86_get_cr4() | X86_CR4_VMXE;
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
        return ERR_BAD_STATE;
    }

    // Enable VMX using the VMXE bit.
    x86_set_cr4(cr4);

    // Execute VMXON.
    return per_cpu->VmxOn();
}

status_t PerCpu::Init(const VmxInfo& info) {
    status_t status = page_.Alloc(info);
    if (status != NO_ERROR)
        return status;

    VmxRegion* region = page_.VirtualAddress<VmxRegion>();
    region->revision_id = info.revision_id;
    return NO_ERROR;
}

status_t VmxonPerCpu::VmxOn() {
    status_t status = vmxon(page_.PhysicalAddress());
    is_on_ = status == NO_ERROR;
    return status;
}

status_t VmxonPerCpu::VmxOff() {
    return is_on_ ? vmxoff() : NO_ERROR;
}

// static
status_t VmxonContext::Create(mxtl::unique_ptr<VmxonContext>* context) {
    uint num_cpus = arch_max_num_cpus();

    AllocChecker ac;
    VmxonPerCpu* ctxs = new (&ac) VmxonPerCpu[num_cpus];
    if (!ac.check())
        return ERR_NO_MEMORY;

    mxtl::Array<VmxonPerCpu> cpu_ctxs(ctxs, num_cpus);
    mxtl::unique_ptr<VmxonContext> ctx(new (&ac) VmxonContext(mxtl::move(cpu_ctxs)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    VmxInfo vmx_info;
    status_t status = InitPerCpus(vmx_info, &ctx->per_cpus_);
    if (status != NO_ERROR)
        return status;

    status = percpu_exec(vmx_enable, ctx.get());
    if (status != NO_ERROR)
        return status;

    *context = mxtl::move(ctx);
    return NO_ERROR;
}

VmxonContext::VmxonContext(mxtl::Array<VmxonPerCpu> per_cpus)
    : per_cpus_(mxtl::move(per_cpus)) {}

static int vmx_disable(void* arg) {
    VmxonContext* context = static_cast<VmxonContext*>(arg);
    VmxonPerCpu* per_cpu = context->PerCpu();

    // Execute VMXOFF.
    status_t status = per_cpu->VmxOff();
    if (status != NO_ERROR)
        return status;

    // Disable VMX.
    x86_set_cr4(x86_get_cr4() & ~X86_CR4_VMXE);
    return NO_ERROR;
}

VmxonContext::~VmxonContext() {
    __UNUSED status_t status = percpu_exec(vmx_disable, this);
    DEBUG_ASSERT(status == NO_ERROR);
}

VmxonPerCpu* VmxonContext::PerCpu() {
    return &per_cpus_[arch_curr_cpu_num()];
}

AutoVmcsLoad::AutoVmcsLoad(VmxPage* page) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();
    __UNUSED status_t status = vmptrld(page->PhysicalAddress());
    DEBUG_ASSERT(status == NO_ERROR);
}

AutoVmcsLoad::~AutoVmcsLoad() {
    DEBUG_ASSERT(arch_ints_disabled());
    arch_enable_ints();
}

static status_t set_vmcs_control(uint32_t controls, uint64_t true_msr, uint64_t old_msr,
                                 uint32_t set) {
    uint32_t allowed_0 = static_cast<uint32_t>(BITS(true_msr, 31, 0));
    uint32_t allowed_1 = static_cast<uint32_t>(BITS_SHIFT(true_msr, 63, 32));
    if ((allowed_1 & set) != set) {
        dprintf(SPEW, "can not set vmcs controls %#" PRIx32 "\n", controls);
        return ERR_NOT_SUPPORTED;
    }

    // Reference Volume 3, Section 31.5.1, Algorithm 3, Part C. If the control
    // can be either 0 or 1 (flexible), and the control is unknown, then refer
    // to the old MSR to find the default value.
    //
    // NOTE: We do not explicitly clear any controls, otherwise unknown would be
    // defined as the complement of the union of what is set and cleared.
    uint32_t flexible = allowed_0 ^ allowed_1;
    uint32_t unknown = flexible & ~set;
    uint32_t defaults = unknown & BITS(old_msr, 31, 0);
    vmwrite(controls, allowed_0 | defaults | set);
    return NO_ERROR;
}

status_t VmcsPerCpu::Clear() {
    return page_.IsAllocated() ? vmclear(page_.PhysicalAddress()) : NO_ERROR;
}

static uint64_t ept_pointer(paddr_t pml4_address) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(pml4_address));
    return
        // Physical address of the PML4 page, page aligned.
        pml4_address |
        // Use write back memory.
        VMX_MEMORY_TYPE_WRITE_BACK << 0 |
        // Page walk length of 4 (defined as N minus 1).
        3u << 3 |
        // Accessed and dirty flags are enabled.
        1u << 6;
}

status_t VmcsPerCpu::Setup(paddr_t pml4_address) {
    status_t status = Clear();
    if (status != NO_ERROR)
        return status;

    AutoVmcsLoad vmcs_load(&page_);

    // Setup secondary processor-based VMCS controls.
    status = set_vmcs_control(VMCS_32_PROCBASED_CTLS2,
                              read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                              0,
                              // Enable use of extended page tables.
                              VMCS_32_PROCBASED_CTLS2_EPT |
                              // Enable use of RDTSCP instruction.
                              VMCS_32_PROCBASED_CTLS2_RDTSCP |
                              // Associate cached translations of linear
                              // addresses with a virtual processor ID.
                              VMCS_32_PROCBASED_CTLS2_VPID |
                              // Enable use of XSAVES and XRSTORS instructions.
                              VMCS_32_PROCBASED_CTLS2_XSAVES_XRSTORS);
    if (status != NO_ERROR)
        return status;

    // Setup pin-based VMCS controls.
    status = set_vmcs_control(VMCS_32_PINBASED_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS),
                              read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS),
                              // External interrupts cause a VM exit.
                              VMCS_32_PINBASED_CTLS_EXTINT_EXITING |
                              // Non-maskable interrupts cause a VM exit.
                              VMCS_32_PINBASED_CTLS_NMI_EXITING);
    if (status != NO_ERROR)
        return status;

    // Setup primary processor-based VMCS controls.
    status = set_vmcs_control(VMCS_32_PROCBASED_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS),
                              read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS),
                              // IO instructions cause a VM exit.
                              VMCS_32_PROCBASED_CTLS_IO_EXITING |
                              // Enable secondary processor-based controls.
                              VMCS_32_PROCBASED_CTLS_PROCBASED_CTLS2);
    if (status != NO_ERROR)
        return status;

    // Setup VM-exit VMCS controls.
    status = set_vmcs_control(VMCS_32_EXIT_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_EXIT_CTLS),
                              read_msr(X86_MSR_IA32_VMX_EXIT_CTLS),
                              // Logical processor is in 64-bit mode after VM
                              // exit. On VM exit CS.L, IA32_EFER.LME, and
                              // IA32_EFER.LMA is set to true.
                              VMCS_32_EXIT_CTLS_64BIT_MODE |
                              // Save the guest IA32_PAT MSR on exit.
                              VMCS_32_EXIT_CTLS_SAVE_IA32_PAT |
                              // Load the host IA32_PAT MSR on exit.
                              VMCS_32_EXIT_CTLS_LOAD_IA32_PAT |
                              // Save the guest IA32_EFER MSR on exit.
                              VMCS_32_EXIT_CTLS_SAVE_IA32_EFER |
                              // Load the host IA32_EFER MSR on exit.
                              VMCS_32_EXIT_CTLS_LOAD_IA32_EFER);
    if (status != NO_ERROR)
        return status;

    // Setup VM-entry VMCS controls.
    status = set_vmcs_control(VMCS_32_ENTRY_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                              read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS),
                              // After VM entry, logical processor is in IA-32e
                              // mode and IA32_EFER.LMA is set to true.
                              VMCS_32_ENTRY_CTLS_IA32E_MODE |
                              // Load the guest IA32_PAT MSR on entry.
                              VMCS_32_ENTRY_CTLS_LOAD_IA32_PAT |
                              // Load the guest IA32_EFER MSR on entry.
                              VMCS_32_ENTRY_CTLS_LOAD_IA32_EFER);
    if (status != NO_ERROR)
        return status;

    // From Volume 3, Section 24.6.3: The exception bitmap is a 32-bit field
    // that contains one bit for each exception. When an exception occurs,
    // its vector is used to select a bit in this field. If the bit is 1,
    // the exception causes a VM exit. If the bit is 0, the exception is
    // delivered normally through the IDT, using the descriptor
    // corresponding to the exception’s vector.
    //
    // From Volume 3, Section 25.2: If software desires VM exits on all page
    // faults, it can set bit 14 in the exception bitmap to 1 and set the
    // page-fault error-code mask and match fields each to 00000000H.
    vmwrite(VMCS_32_EXCEPTION_BITMAP, VMCS_32_EXCEPTION_BITMAP_ALL_EXCEPTIONS);
    vmwrite(VMCS_32_PAGEFAULT_ERRORCODE_MASK, 0);
    vmwrite(VMCS_32_PAGEFAULT_ERRORCODE_MATCH, 0);

    // From Volume 3, Section 28.1: Virtual-processor identifiers (VPIDs)
    // introduce to VMX operation a facility by which a logical processor may
    // cache information for multiple linear-address spaces. When VPIDs are
    // used, VMX transitions may retain cached information and the logical
    // processor switches to a different linear-address space.
    //
    // From Volume 3, Section 26.2.1.1: If the “enable VPID” VM-execution
    // control is 1, the value of the VPID VM-execution control field must not
    // be 0000H.
    //
    // From Volume 3, Section 28.3.3.3: If EPT is in use, the logical processor
    // associates all mappings it creates with the value of bits 51:12 of
    // current EPTP. If a VMM uses different EPTP values for different guests,
    // it may use the same VPID for those guests.
    x86_percpu* percpu = x86_get_percpu();
    vmwrite(VMCS_16_VPID, percpu->cpu_num + 1);

    // From Volume 3, Section 28.2: The extended page-table mechanism (EPT) is a
    // feature that can be used to support the virtualization of physical
    // memory. When EPT is in use, certain addresses that would normally be
    // treated as physical addresses (and used to access memory) are instead
    // treated as guest-physical addresses. Guest-physical addresses are
    // translated by traversing a set of EPT paging structures to produce
    // physical addresses that are used to access memory.
    vmwrite(VMCS_64_EPT_POINTER, ept_pointer(pml4_address));

    // Setup VMCS host state.
    //
    // NOTE: We are pinned to a thread when executing this function, therefore
    // it is acceptable to use per-CPU state.
    vmwrite(VMCS_64_HOST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
    vmwrite(VMCS_64_HOST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));
    vmwrite(VMCS_XX_HOST_CR0, x86_get_cr0());
    vmwrite(VMCS_XX_HOST_CR4, x86_get_cr4());
    vmwrite(VMCS_16_HOST_ES_SELECTOR, 0);
    vmwrite(VMCS_16_HOST_CS_SELECTOR, CODE_64_SELECTOR);
    vmwrite(VMCS_16_HOST_SS_SELECTOR, DATA_SELECTOR);
    vmwrite(VMCS_16_HOST_DS_SELECTOR, 0);
    vmwrite(VMCS_16_HOST_FS_SELECTOR, 0);
    vmwrite(VMCS_16_HOST_GS_SELECTOR, 0);
    vmwrite(VMCS_16_HOST_TR_SELECTOR, TSS_SELECTOR(percpu->cpu_num));
    vmwrite(VMCS_XX_HOST_FS_BASE, read_msr(X86_MSR_IA32_FS_BASE));
    vmwrite(VMCS_XX_HOST_GS_BASE, read_msr(X86_MSR_IA32_GS_BASE));
    vmwrite(VMCS_XX_HOST_TR_BASE, reinterpret_cast<uint64_t>(&percpu->default_tss));
    vmwrite(VMCS_XX_HOST_GDTR_BASE, reinterpret_cast<uint64_t>(_gdt));
    vmwrite(VMCS_XX_HOST_IDTR_BASE, reinterpret_cast<uint64_t>(idt_get_readonly()));
    vmwrite(VMCS_XX_HOST_IA32_SYSENTER_ESP, 0);
    vmwrite(VMCS_XX_HOST_IA32_SYSENTER_EIP, 0);
    vmwrite(VMCS_32_HOST_IA32_SYSENTER_CS, 0);
    vmwrite(VMCS_XX_HOST_RSP, reinterpret_cast<uint64_t>(&vmx_state_));
    vmwrite(VMCS_XX_HOST_RIP, reinterpret_cast<uint64_t>(vmx_exit_entry));

    vmx_state_.host_state.star = read_msr(X86_MSR_IA32_STAR);
    vmx_state_.host_state.lstar = read_msr(X86_MSR_IA32_LSTAR);
    vmx_state_.host_state.fmask = read_msr(X86_MSR_IA32_FMASK);

    // Setup VMCS guest state.

    uint64_t cr0 = X86_CR0_PE | // Enable protected mode
                   X86_CR0_PG | // Enable paging
                   X86_CR0_NE;  // Enable internal x87 exception handling
    if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1)) {
        return ERR_BAD_STATE;
    }
    vmwrite(VMCS_XX_GUEST_CR0, cr0);

    uint64_t cr4 = X86_CR4_PAE |  // Enable PAE paging
                   X86_CR4_VMXE;  // Enable VMX
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
        return ERR_BAD_STATE;
    }
    vmwrite(VMCS_XX_GUEST_CR4, cr4);

    vmwrite(VMCS_64_GUEST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
    vmwrite(VMCS_64_GUEST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));

    vmwrite(VMCS_32_GUEST_CS_ACCESS_RIGHTS,
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_A |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_W |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_E |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_TYPE_CODE |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_S |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_P |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_L);

    vmwrite(VMCS_32_GUEST_TR_ACCESS_RIGHTS,
            VMCS_32_GUEST_TR_ACCESS_RIGHTS_TSS_BUSY |
            VMCS_32_GUEST_XX_ACCESS_RIGHTS_P);

    // Disable all other segment selectors until we have a guest that uses them.
    vmwrite(VMCS_32_GUEST_SS_ACCESS_RIGHTS, VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmwrite(VMCS_32_GUEST_DS_ACCESS_RIGHTS, VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmwrite(VMCS_32_GUEST_ES_ACCESS_RIGHTS, VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmwrite(VMCS_32_GUEST_FS_ACCESS_RIGHTS, VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmwrite(VMCS_32_GUEST_GS_ACCESS_RIGHTS, VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmwrite(VMCS_32_GUEST_LDTR_ACCESS_RIGHTS, VMCS_32_GUEST_XX_ACCESS_RIGHTS_UNUSABLE);

    vmwrite(VMCS_XX_GUEST_GDTR_BASE, 0);
    vmwrite(VMCS_32_GUEST_GDTR_LIMIT, 0);
    vmwrite(VMCS_XX_GUEST_IDTR_BASE, 0);
    vmwrite(VMCS_32_GUEST_IDTR_LIMIT, 0);

    // Set all reserved RFLAGS bits to their correct values
    vmwrite(VMCS_XX_GUEST_RFLAGS, X86_FLAGS_RESERVED_ONES);

    vmwrite(VMCS_32_GUEST_ACTIVITY_STATE, 0);
    vmwrite(VMCS_32_GUEST_INTERRUPTIBILITY_STATE, 0);
    vmwrite(VMCS_XX_GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

    // From Volume 3, Section 26.3.1.1: The IA32_SYSENTER_ESP field and the
    // IA32_SYSENTER_EIP field must each contain a canonical address.
    vmwrite(VMCS_XX_GUEST_IA32_SYSENTER_ESP, 0);
    vmwrite(VMCS_XX_GUEST_IA32_SYSENTER_EIP, 0);

    vmwrite(VMCS_32_GUEST_IA32_SYSENTER_CS, 0);
    vmwrite(VMCS_XX_GUEST_RSP, 0);

    // From Volume 3, Section 24.4.2: If the “VMCS shadowing” VM-execution
    // control is 1, the VMREAD and VMWRITE instructions access the VMCS
    // referenced by this pointer (see Section 24.10). Otherwise, software
    // should set this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry
    // failures (see Section 26.3.1.5).
    vmwrite(VMCS_64_LINK_POINTER, VMCS_64_LINK_POINTER_INVALIDATE);

    return NO_ERROR;
}

void vmx_exit(VmxState* vmx_state) {
    DEBUG_ASSERT(arch_ints_disabled());
    uint cpu_num = arch_curr_cpu_num();

    // Reload the task segment in order to restore its limit. VMX always
    // restores it with a limit of 0x67, which excludes the IO bitmap.
    seg_sel_t selector = TSS_SELECTOR(cpu_num);
    x86_clear_tss_busy(selector);
    x86_ltr(selector);

    // Reload the interrupt descriptor table in order to restore its limit. VMX
    // always restores it with a limit of 0xffff, which is too large.
    idt_load(idt_get_readonly());

    // TODO(abdulla): Optimise this, and don't do it unconditionally.
    write_msr(X86_MSR_IA32_STAR, vmx_state->host_state.star);
    write_msr(X86_MSR_IA32_LSTAR, vmx_state->host_state.lstar);
    write_msr(X86_MSR_IA32_FMASK, vmx_state->host_state.fmask);
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, vmx_state->host_state.kernel_gs_base);
}

static status_t vmexit_handler(uint64_t reason, uint64_t qualification, uint64_t next_rip,
                               const VmxState& vmx_state, FifoDispatcher* serial_fifo) {
    switch (reason) {
    case VMCS_32_EXIT_REASON_IO_INSTRUCTION: {
        dprintf(SPEW, "handling IO instruction\n");
        vmwrite(VMCS_XX_GUEST_RIP, next_rip);
        uint16_t io_port = (qualification >> VMCS_XX_EXIT_QUALIFICATION_IO_PORT_SHIFT) &
                           VMCS_XX_EXIT_QUALIFICATION_IO_PORT_MASK;
        if (io_port != kUartIoPort)
            return NO_ERROR;
        uint8_t byte = vmx_state.guest_state.rax & 0xff;
        uint32_t actual;
        return serial_fifo->Write(&byte, 1, &actual);
    }
    case VMCS_32_EXIT_REASON_EXTERNAL_INTERRUPT:
        dprintf(SPEW, "enabling interrupts for external interrupt\n");
        DEBUG_ASSERT(arch_ints_disabled());
        arch_enable_ints();
        arch_disable_ints();
        break;
    }
    return NO_ERROR;
}

status_t VmcsPerCpu::Enter(const VmcsContext& context, FifoDispatcher* serial_fifo) {
    AutoVmcsLoad vmcs_load(&page_);
    // FS is used for thread-local storage — save for this thread.
    vmwrite(VMCS_XX_HOST_FS_BASE, read_msr(X86_MSR_IA32_FS_BASE));
    // CR3 is used to maintain the virtual address space — save for this thread.
    vmwrite(VMCS_XX_HOST_CR3, x86_get_cr3());
    // Kernel GS stores the user-space GS (within the kernel) — as the calling
    // user-space thread may change, save this every time.
    vmx_state_.host_state.kernel_gs_base = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);

    if (do_resume_) {
        dprintf(SPEW, "re-entering guest\n");
    } else {
        vmwrite(VMCS_XX_GUEST_CR3, context.cr3());
        vmwrite(VMCS_XX_GUEST_RIP, context.entry());
    }

    status_t status = vmx_enter(&vmx_state_, do_resume_);
    if (status != NO_ERROR) {
        uint64_t error = vmread(VMCS_32_INSTRUCTION_ERROR);
        dprintf(SPEW, "vmlaunch failed: %#" PRIx64 "\n", error);
    } else {
        uint64_t reason = vmread(VMCS_32_EXIT_REASON);
        dprintf(SPEW, "vmexit reason: %#" PRIx64 "\n", reason);
        uint64_t qualification = vmread(VMCS_XX_EXIT_QUALIFICATION);
        dprintf(SPEW, "vmexit qualification: %#" PRIx64 "\n", qualification);
        uint64_t interruption_information = vmread(VMCS_32_INTERRUPTION_INFORMATION);
        dprintf(SPEW, "vmexit interruption information: %#" PRIx64 "\n", interruption_information);
        uint64_t interruption_error_code = vmread(VMCS_32_INTERRUPTION_ERROR_CODE);
        dprintf(SPEW, "vmexit interruption error code: %#" PRIx64 "\n", interruption_error_code);
        uint64_t instruction_length = vmread(VMCS_32_INSTRUCTION_LENGTH);
        dprintf(SPEW, "vmexit instruction length: %#" PRIx64 "\n", instruction_length);
        uint64_t instruction_information = vmread(VMCS_32_INSTRUCTION_INFORMATION);
        dprintf(SPEW, "vmexit instruction information: %#" PRIx64 "\n", instruction_information);

        uint64_t physical_address = vmread(VMCS_64_GUEST_PHYSICAL_ADDRESS);
        dprintf(SPEW, "guest physical address: %#" PRIx64 "\n", physical_address);
        uint64_t linear_address = vmread(VMCS_XX_GUEST_LINEAR_ADDRESS);
        dprintf(SPEW, "guest linear address: %#" PRIx64 "\n", linear_address);
        uint64_t interruptibility_state = vmread(VMCS_32_GUEST_INTERRUPTIBILITY_STATE);
        dprintf(SPEW, "guest interruptibility state: %#" PRIx64 "\n", interruptibility_state);
        uint64_t rip = vmread(VMCS_XX_GUEST_RIP);
        dprintf(SPEW, "guest rip: %#" PRIx64 "\n", rip);

        do_resume_ = true;
        status = vmexit_handler(reason, qualification, rip + instruction_length,
                                vmx_state_, serial_fifo);
    }
    return status;
}

static int vmcs_setup(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsPerCpu* per_cpu = context->PerCpu();
    return per_cpu->Setup(context->Pml4Address());
}

// static
status_t VmcsContext::Create(mxtl::RefPtr<VmObject> guest_phys_mem,
                             mxtl::RefPtr<FifoDispatcher> serial_fifo,
                             mxtl::unique_ptr<VmcsContext>* context) {
    uint num_cpus = arch_max_num_cpus();

    AllocChecker ac;
    VmcsPerCpu* ctxs = new (&ac) VmcsPerCpu[num_cpus];
    if (!ac.check())
        return ERR_NO_MEMORY;

    mxtl::Array<VmcsPerCpu> cpu_ctxs(ctxs, num_cpus);
    mxtl::unique_ptr<VmcsContext> ctx(new (&ac) VmcsContext(serial_fifo, mxtl::move(cpu_ctxs)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t status = GuestPhysicalAddressSpace::Create(guest_phys_mem, &ctx->gpas_);
    if (status != NO_ERROR)
        return status;

    VmxInfo vmx_info;
    status = InitPerCpus(vmx_info, &ctx->per_cpus_);
    if (status != NO_ERROR)
        return status;

    status = percpu_exec(vmcs_setup, ctx.get());
    if (status != NO_ERROR)
        return status;

    *context = mxtl::move(ctx);
    return NO_ERROR;
}

VmcsContext::VmcsContext(mxtl::RefPtr<FifoDispatcher> serial_fifo,
                         mxtl::Array<VmcsPerCpu> per_cpus)
    : serial_fifo_(serial_fifo), per_cpus_(mxtl::move(per_cpus)) {}

static int vmcs_clear(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsPerCpu* per_cpu = context->PerCpu();
    return per_cpu->Clear();
}

VmcsContext::~VmcsContext() {
    __UNUSED status_t status = percpu_exec(vmcs_clear, this);
    DEBUG_ASSERT(status == NO_ERROR);
}

paddr_t VmcsContext::Pml4Address() {
    return gpas_->Pml4Address();
}

VmcsPerCpu* VmcsContext::PerCpu() {
    return &per_cpus_[arch_curr_cpu_num()];
}

status_t VmcsContext::set_cr3(uintptr_t guest_cr3) {
    if (guest_cr3 >= gpas_->size() - PAGE_SIZE)
        return ERR_INVALID_ARGS;
    cr3_ = guest_cr3;
    return NO_ERROR;
}

status_t VmcsContext::set_entry(uintptr_t guest_entry) {
    if (guest_entry >= gpas_->size())
        return ERR_INVALID_ARGS;
    entry_ = guest_entry;
    return NO_ERROR;
}

static int vmcs_launch(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsPerCpu* per_cpu = context->PerCpu();
    return per_cpu->Enter(*context, context->serial_fifo());
}

status_t VmcsContext::Enter() {
    if (cr3_ == UINTPTR_MAX)
        return ERR_BAD_STATE;
    if (entry_ == UINTPTR_MAX)
        return ERR_BAD_STATE;
    return percpu_exec(vmcs_launch, this);
}

status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    // Check that the CPU supports VMX.
    if (!x86_feature_test(X86_FEATURE_VMX))
        return ERR_NOT_SUPPORTED;

    return VmxonContext::Create(context);
}

status_t arch_guest_create(mxtl::RefPtr<VmObject> guest_phys_mem,
                           mxtl::RefPtr<FifoDispatcher> serial_fifo,
                           mxtl::unique_ptr<GuestContext>* context) {
    return VmcsContext::Create(guest_phys_mem, serial_fifo, context);
}

status_t arch_guest_enter(const mxtl::unique_ptr<GuestContext>& context) {
    return context->Enter();
}

status_t x86_guest_set_cr3(const mxtl::unique_ptr<GuestContext>& context, uintptr_t guest_cr3) {
    return context->set_cr3(guest_cr3);
}

status_t arch_guest_set_entry(const mxtl::unique_ptr<GuestContext>& context,
                              uintptr_t guest_entry) {
    return context->set_entry(guest_entry);
}
