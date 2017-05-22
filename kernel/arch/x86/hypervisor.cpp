// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <string.h>

#include <arch/hypervisor.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <hypervisor/guest_physical_address_space.h>
#include <magenta/syscalls/hypervisor.h>
#include <mxalloc/new.h>

#if WITH_LIB_MAGENTA
#include <magenta/fifo_dispatcher.h>
#else
class FifoDispatcher : public mxtl::RefCounted<FifoDispatcher> {};
#endif // WITH_LIB_MAGENTA

#include "hypervisor_priv.h"
#include "vmexit_priv.h"

#define VMX_ERR_CHECK(var) \
    "setna %[" #var "];"     // Check CF and ZF for error.

extern uint8_t _gdt[];
static const uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;

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

uint16_t vmcs_read(VmcsField16 field) {
    return static_cast<uint16_t>(vmread(static_cast<uint64_t>(field)));
}

uint32_t vmcs_read(VmcsField32 field) {
    return static_cast<uint32_t>(vmread(static_cast<uint64_t>(field)));
}

uint64_t vmcs_read(VmcsField64 field) {
    return vmread(static_cast<uint64_t>(field));
}

uint64_t vmcs_read(VmcsFieldXX field) {
    return vmread(static_cast<uint64_t>(field));
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

void vmcs_write(VmcsField16 field, uint16_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

void vmcs_write(VmcsField32 field, uint32_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

void vmcs_write(VmcsField64 field, uint64_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

void vmcs_write(VmcsFieldXX field, uint64_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
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

template<typename T>
static status_t InitPerCpus(const VmxInfo& vmx_info, mxtl::Array<T>* ctxs) {
    for (size_t i = 0; i < ctxs->size(); i++) {
        status_t status = (*ctxs)[i].Init(vmx_info);
        if (status != NO_ERROR)
            return status;
    }
    return NO_ERROR;
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

MiscInfo::MiscInfo() {
    // From Volume 3, Appendix A.6.
    uint64_t misc_info = read_msr(X86_MSR_IA32_VMX_MISC);
    wait_for_sipi = BIT_SHIFT(misc_info, 8);
    msr_list_limit = static_cast<uint32_t>(BITS_SHIFT(misc_info, 27, 25) + 1) * 512;
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

status_t VmxPage::Alloc(const VmxInfo& vmx_info, uint8_t fill) {
    // From Volume 3, Appendix A.1: Bits 44:32 report the number of bytes that
    // software should allocate for the VMXON region and any VMCS region. It is
    // a value greater than 0 and at most 4096 (bit 44 is set if and only if
    // bits 43:32 are clear).
    if (vmx_info.region_size > PAGE_SIZE)
        return ERR_NOT_SUPPORTED;

    // Check use of write-back memory for VMX regions is supported.
    if (!vmx_info.write_back)
        return ERR_NOT_SUPPORTED;

    // The maximum size for a VMXON or VMCS region is 4096, therefore
    // unconditionally allocating a page is adequate.
    if (pmm_alloc_page(0, &pa_) == nullptr)
        return ERR_NO_MEMORY;

    DEBUG_ASSERT(IS_PAGE_ALIGNED(pa_));
    memset(VirtualAddress(), fill, PAGE_SIZE);
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

status_t PerCpu::Init(const VmxInfo& info) {
    status_t status = page_.Alloc(info, 0);
    if (status != NO_ERROR)
        return status;

    VmxRegion* region = page_.VirtualAddress<VmxRegion>();
    region->revision_id = info.revision_id;
    return NO_ERROR;
}

AutoVmcsLoad::AutoVmcsLoad(VmxPage* page)
    : page_(page) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();
    __UNUSED status_t status = vmptrld(page_->PhysicalAddress());
    DEBUG_ASSERT(status == NO_ERROR);
}

AutoVmcsLoad::~AutoVmcsLoad() {
    DEBUG_ASSERT(arch_ints_disabled());
    arch_enable_ints();
}

void AutoVmcsLoad::reload() {
    // When we VM exit due to an external interrupt, we want to handle that
    // interrupt. To do that, we temporarily re-enable interrupts. However, we
    // must then reload the VMCS, in case it has been changed in the interim.
    DEBUG_ASSERT(arch_ints_disabled());
    arch_enable_ints();
    arch_disable_ints();
    __UNUSED status_t status = vmptrld(page_->PhysicalAddress());
    DEBUG_ASSERT(status == NO_ERROR);
}

status_t VmxonPerCpu::VmxOn() {
    status_t status = vmxon(page_.PhysicalAddress());
    is_on_ = status == NO_ERROR;
    return status;
}

status_t VmxonPerCpu::VmxOff() {
    return is_on_ ? vmxoff() : NO_ERROR;
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

    // Check that wait for startup IPI is a supported activity state.
    MiscInfo misc_info;
    if (!misc_info.wait_for_sipi)
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

status_t VmcsPerCpu::Init(const VmxInfo& vmx_info) {
    status_t status = PerCpu::Init(vmx_info);
    if (status != NO_ERROR)
        return status;

    status = host_msr_page_.Alloc(vmx_info, 0);
    if (status != NO_ERROR)
        return status;

    status = guest_msr_page_.Alloc(vmx_info, 0);
    if (status != NO_ERROR)
        return status;

    memset(&vmx_state_, 0, sizeof(vmx_state_));
    timer_initialize(&local_apic_state_.timer);
    event_init(&local_apic_state_.event, false, EVENT_FLAG_AUTOUNSIGNAL);
    local_apic_state_.active_interrupt = kInvalidInterrupt;
    local_apic_state_.tsc_deadline = 0;
    local_apic_state_.apic_addr = nullptr;
    return NO_ERROR;
}

status_t VmcsPerCpu::Clear() {
    return page_.IsAllocated() ? vmclear(page_.PhysicalAddress()) : NO_ERROR;
}

static status_t set_vmcs_control(VmcsField32 controls, uint64_t true_msr, uint64_t old_msr,
                                 uint32_t set, uint32_t clear) {
    uint32_t allowed_0 = static_cast<uint32_t>(BITS(true_msr, 31, 0));
    uint32_t allowed_1 = static_cast<uint32_t>(BITS_SHIFT(true_msr, 63, 32));
    if ((allowed_1 & set) != set) {
        dprintf(SPEW, "can not set vmcs controls %#x\n", static_cast<uint>(controls));
        return ERR_NOT_SUPPORTED;
    }
    if ((~allowed_0 & clear) != clear) {
        dprintf(SPEW, "can not clear vmcs controls %#x\n", static_cast<uint>(controls));
        return ERR_NOT_SUPPORTED;
    }
    if ((set & clear) != 0) {
        dprintf(SPEW, "can not set and clear the same vmcs controls %#x\n",
                static_cast<uint>(controls));
        return ERR_INVALID_ARGS;
    }

    // Reference Volume 3, Section 31.5.1, Algorithm 3, Part C. If the control
    // can be either 0 or 1 (flexible), and the control is unknown, then refer
    // to the old MSR to find the default value.
    uint32_t flexible = allowed_0 ^ allowed_1;
    uint32_t unknown = flexible & ~(set | clear);
    uint32_t defaults = unknown & BITS(old_msr, 31, 0);
    vmcs_write(controls, allowed_0 | defaults | set);
    return NO_ERROR;
}

static uint64_t ept_pointer(paddr_t pml4_address) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(pml4_address));
    return
        // Physical address of the PML4 page, page aligned.
        pml4_address |
        // Use write back memory.
        VMX_MEMORY_TYPE_WRITE_BACK << 0 |
        // Page walk length of 4 (defined as N minus 1).
        3u << 3;
}

static void ignore_msr(VmxPage* msr_bitmaps_page, uint32_t msr) {
    // From Volume 3, Section 24.6.9.
    uint8_t* msr_bitmaps = msr_bitmaps_page->VirtualAddress<uint8_t>();
    if (msr >= 0xc0000000)
        msr_bitmaps += 1 << 10;

    uint16_t msr_low = msr & 0x1fff;
    uint16_t msr_byte = msr_low / 8;
    uint8_t msr_bit = msr_low % 8;

    // Ignore reads to the MSR.
    msr_bitmaps[msr_byte] &= (uint8_t)~(1 << msr_bit);

    // Ignore writes to the MSR.
    msr_bitmaps += 2 << 10;
    msr_bitmaps[msr_byte] &= (uint8_t)~(1 << msr_bit);
}

struct MsrListEntry {
    uint32_t msr;
    uint32_t reserved;
    uint64_t value;
} __PACKED;

static void edit_msr_list(VmxPage* msr_list_page, uint index, uint32_t msr, uint64_t value) {
    // From Volume 3, Section 24.7.2.

    // From Volume 3, Appendix A.6: Specifically, if the value bits 27:25 of
    // IA32_VMX_MISC is N, then 512 * (N + 1) is the recommended maximum number
    // of MSRs to be included in each list.
    //
    // From Volume 3, Section 24.7.2: This field specifies the number of MSRs to
    // be stored on VM exit. It is recommended that this count not exceed 512
    // bytes.
    //
    // Since these two statements conflict, we are taking the conservative
    // minimum and asserting that: index < (512 bytes / size of MsrListEntry).
    ASSERT(index < (512 / sizeof(MsrListEntry)));

    MsrListEntry* entry = msr_list_page->VirtualAddress<MsrListEntry>() + index;
    entry->msr = msr;
    entry->value = value;
}

status_t VmcsPerCpu::Setup(paddr_t pml4_address, paddr_t apic_access_address,
                           paddr_t msr_bitmaps_address) {
    status_t status = Clear();
    if (status != NO_ERROR)
        return status;

    AutoVmcsLoad vmcs_load(&page_);

    // Setup secondary processor-based VMCS controls.
    status = set_vmcs_control(VmcsField32::PROCBASED_CTLS2,
                              read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                              0,
                              // Enable APIC access virtualization.
                              PROCBASED_CTLS2_APIC_ACCESS |
                              // Enable use of extended page tables.
                              PROCBASED_CTLS2_EPT |
                              // Enable use of RDTSCP instruction.
                              PROCBASED_CTLS2_RDTSCP |
                              // Associate cached translations of linear
                              // addresses with a virtual processor ID.
                              PROCBASED_CTLS2_VPID,
                              0);
    if (status != NO_ERROR)
        return status;

    // Setup pin-based VMCS controls.
    status = set_vmcs_control(VmcsField32::PINBASED_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS),
                              read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS),
                              // External interrupts cause a VM exit.
                              PINBASED_CTLS_EXT_INT_EXITING |
                              // Non-maskable interrupts cause a VM exit.
                              PINBASED_CTLS_NMI_EXITING,
                              0);
    if (status != NO_ERROR)
        return status;

    // Setup primary processor-based VMCS controls.
    status = set_vmcs_control(VmcsField32::PROCBASED_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS),
                              read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS),
                              // Enable VM exit when interrupts are enabled.
                              PROCBASED_CTLS_INT_WINDOW_EXITING |
                              // Enable VM exit on HLT instruction.
                              PROCBASED_CTLS_HLT_EXITING |
                              // Enable TPR virtualization.
                              PROCBASED_CTLS_TPR_SHADOW |
                              // Enable VM exit on IO instructions.
                              PROCBASED_CTLS_IO_EXITING |
                              // Enable use of MSR bitmaps.
                              PROCBASED_CTLS_MSR_BITMAPS |
                              // Enable secondary processor-based controls.
                              PROCBASED_CTLS_PROCBASED_CTLS2,
                              // Disable VM exit on CR3 load.
                              PROCBASED_CTLS_CR3_LOAD_EXITING |
                              // Disable VM exit on CR3 store.
                              PROCBASED_CTLS_CR3_STORE_EXITING |
                              // Disable VM exit on CR8 load.
                              PROCBASED_CTLS_CR8_LOAD_EXITING |
                              // Disable VM exit on CR8 store.
                              PROCBASED_CTLS_CR8_STORE_EXITING);
    if (status != NO_ERROR)
        return status;

    // We only enable interrupt-window exiting above to ensure that the
    // processor supports it for later use. So disable it for now.
    interrupt_window_exiting(false);

    // Setup VM-exit VMCS controls.
    status = set_vmcs_control(VmcsField32::EXIT_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_EXIT_CTLS),
                              read_msr(X86_MSR_IA32_VMX_EXIT_CTLS),
                              // Logical processor is in 64-bit mode after VM
                              // exit. On VM exit CS.L, IA32_EFER.LME, and
                              // IA32_EFER.LMA is set to true.
                              EXIT_CTLS_64BIT_MODE |
                              // Save the guest IA32_PAT MSR on exit.
                              EXIT_CTLS_SAVE_IA32_PAT |
                              // Load the host IA32_PAT MSR on exit.
                              EXIT_CTLS_LOAD_IA32_PAT |
                              // Save the guest IA32_EFER MSR on exit.
                              EXIT_CTLS_SAVE_IA32_EFER |
                              // Load the host IA32_EFER MSR on exit.
                              EXIT_CTLS_LOAD_IA32_EFER,
                              0);
    if (status != NO_ERROR)
        return status;

    // Setup VM-entry VMCS controls.
    status = set_vmcs_control(VmcsField32::ENTRY_CTLS,
                              read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                              read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS),
                              // After VM entry, logical processor is in IA-32e
                              // mode and IA32_EFER.LMA is set to true.
                              ENTRY_CTLS_IA32E_MODE |
                              // Load the guest IA32_PAT MSR on entry.
                              ENTRY_CTLS_LOAD_IA32_PAT |
                              // Load the guest IA32_EFER MSR on entry.
                              ENTRY_CTLS_LOAD_IA32_EFER,
                              0);
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
    vmcs_write(VmcsField32::EXCEPTION_BITMAP, 0);
    vmcs_write(VmcsField32::PAGEFAULT_ERRORCODE_MASK, 0);
    vmcs_write(VmcsField32::PAGEFAULT_ERRORCODE_MATCH, 0);

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
    vmcs_write(VmcsField16::VPID, static_cast<uint16_t>(percpu->cpu_num + 1));

    // From Volume 3, Section 28.2: The extended page-table mechanism (EPT) is a
    // feature that can be used to support the virtualization of physical
    // memory. When EPT is in use, certain addresses that would normally be
    // treated as physical addresses (and used to access memory) are instead
    // treated as guest-physical addresses. Guest-physical addresses are
    // translated by traversing a set of EPT paging structures to produce
    // physical addresses that are used to access memory.
    vmcs_write(VmcsField64::EPT_POINTER, ept_pointer(pml4_address));

    // Setup APIC handling.
    vmcs_write(VmcsField64::APIC_ACCESS_ADDRESS, apic_access_address);
    vmcs_write(VmcsField64::VIRTUAL_APIC_ADDRESS, vaddr_to_paddr(local_apic_state_.apic_addr));

    // Setup MSR handling.
    vmcs_write(VmcsField64::MSR_BITMAPS_ADDRESS, msr_bitmaps_address);

    // NOTE: Host X86_MSR_IA32_KERNEL_GS_BASE, is set in VmcsPerCpu::Enter.
    edit_msr_list(&host_msr_page_, 1, X86_MSR_IA32_STAR, read_msr(X86_MSR_IA32_STAR));
    edit_msr_list(&host_msr_page_, 2, X86_MSR_IA32_LSTAR, read_msr(X86_MSR_IA32_LSTAR));
    edit_msr_list(&host_msr_page_, 3, X86_MSR_IA32_FMASK, read_msr(X86_MSR_IA32_FMASK));
    edit_msr_list(&host_msr_page_, 4, X86_MSR_IA32_TSC_ADJUST, read_msr(X86_MSR_IA32_TSC_ADJUST));
    vmcs_write(VmcsField64::EXIT_MSR_LOAD_ADDRESS, host_msr_page_.PhysicalAddress());
    vmcs_write(VmcsField32::EXIT_MSR_LOAD_COUNT, 5);

    edit_msr_list(&guest_msr_page_, 0, X86_MSR_IA32_KERNEL_GS_BASE, 0);
    edit_msr_list(&guest_msr_page_, 1, X86_MSR_IA32_STAR, 0);
    edit_msr_list(&guest_msr_page_, 2, X86_MSR_IA32_LSTAR, 0);
    edit_msr_list(&guest_msr_page_, 3, X86_MSR_IA32_FMASK, 0);
    edit_msr_list(&guest_msr_page_, 4, X86_MSR_IA32_TSC_ADJUST, 0);
    vmcs_write(VmcsField64::EXIT_MSR_STORE_ADDRESS, guest_msr_page_.PhysicalAddress());
    vmcs_write(VmcsField32::EXIT_MSR_STORE_COUNT, 5);
    vmcs_write(VmcsField64::ENTRY_MSR_LOAD_ADDRESS, guest_msr_page_.PhysicalAddress());
    vmcs_write(VmcsField32::ENTRY_MSR_LOAD_COUNT, 5);

    // Setup VMCS host state.
    //
    // NOTE: We are pinned to a thread when executing this function, therefore
    // it is acceptable to use per-CPU state.
    vmcs_write(VmcsField64::HOST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
    vmcs_write(VmcsField64::HOST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));
    vmcs_write(VmcsFieldXX::HOST_CR0, x86_get_cr0());
    vmcs_write(VmcsFieldXX::HOST_CR4, x86_get_cr4());
    vmcs_write(VmcsField16::HOST_ES_SELECTOR, 0);
    vmcs_write(VmcsField16::HOST_CS_SELECTOR, CODE_64_SELECTOR);
    vmcs_write(VmcsField16::HOST_SS_SELECTOR, DATA_SELECTOR);
    vmcs_write(VmcsField16::HOST_DS_SELECTOR, 0);
    vmcs_write(VmcsField16::HOST_FS_SELECTOR, 0);
    vmcs_write(VmcsField16::HOST_GS_SELECTOR, 0);
    vmcs_write(VmcsField16::HOST_TR_SELECTOR, TSS_SELECTOR(percpu->cpu_num));
    vmcs_write(VmcsFieldXX::HOST_FS_BASE, read_msr(X86_MSR_IA32_FS_BASE));
    vmcs_write(VmcsFieldXX::HOST_GS_BASE, read_msr(X86_MSR_IA32_GS_BASE));
    vmcs_write(VmcsFieldXX::HOST_TR_BASE, reinterpret_cast<uint64_t>(&percpu->default_tss));
    vmcs_write(VmcsFieldXX::HOST_GDTR_BASE, reinterpret_cast<uint64_t>(_gdt));
    vmcs_write(VmcsFieldXX::HOST_IDTR_BASE, reinterpret_cast<uint64_t>(idt_get_readonly()));
    vmcs_write(VmcsFieldXX::HOST_IA32_SYSENTER_ESP, 0);
    vmcs_write(VmcsFieldXX::HOST_IA32_SYSENTER_EIP, 0);
    vmcs_write(VmcsField32::HOST_IA32_SYSENTER_CS, 0);
    vmcs_write(VmcsFieldXX::HOST_RSP, reinterpret_cast<uint64_t>(&vmx_state_));
    vmcs_write(VmcsFieldXX::HOST_RIP, reinterpret_cast<uint64_t>(vmx_exit_entry));

    // Setup VMCS guest state.
    uint64_t cr0 = X86_CR0_PE | // Enable protected mode
                   X86_CR0_PG | // Enable paging
                   X86_CR0_NE;  // Enable internal x87 exception handling
    if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1)) {
        return ERR_BAD_STATE;
    }
    vmcs_write(VmcsFieldXX::GUEST_CR0, cr0);

    uint64_t cr4 = X86_CR4_PAE |  // Enable PAE paging
                   X86_CR4_VMXE;  // Enable VMX
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
        return ERR_BAD_STATE;
    }
    vmcs_write(VmcsFieldXX::GUEST_CR4, cr4);

    vmcs_write(VmcsField64::GUEST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
    vmcs_write(VmcsField64::GUEST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));

    vmcs_write(VmcsField32::GUEST_CS_ACCESS_RIGHTS,
               GUEST_XX_ACCESS_RIGHTS_TYPE_A |
               GUEST_XX_ACCESS_RIGHTS_TYPE_W |
               GUEST_XX_ACCESS_RIGHTS_TYPE_E |
               GUEST_XX_ACCESS_RIGHTS_TYPE_CODE |
               GUEST_XX_ACCESS_RIGHTS_S |
               GUEST_XX_ACCESS_RIGHTS_P |
               GUEST_XX_ACCESS_RIGHTS_L);

    vmcs_write(VmcsField32::GUEST_TR_ACCESS_RIGHTS,
               GUEST_TR_ACCESS_RIGHTS_TSS_BUSY |
               GUEST_XX_ACCESS_RIGHTS_P);

    // Disable all other segment selectors until we have a guest that uses them.
    vmcs_write(VmcsField32::GUEST_SS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs_write(VmcsField32::GUEST_DS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs_write(VmcsField32::GUEST_ES_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs_write(VmcsField32::GUEST_FS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs_write(VmcsField32::GUEST_GS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs_write(VmcsField32::GUEST_LDTR_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);

    vmcs_write(VmcsFieldXX::GUEST_GDTR_BASE, 0);
    vmcs_write(VmcsField32::GUEST_GDTR_LIMIT, 0);
    vmcs_write(VmcsFieldXX::GUEST_IDTR_BASE, 0);
    vmcs_write(VmcsField32::GUEST_IDTR_LIMIT, 0);

    // Set all reserved RFLAGS bits to their correct values
    vmcs_write(VmcsFieldXX::GUEST_RFLAGS, X86_FLAGS_RESERVED_ONES);

    vmcs_write(VmcsField32::GUEST_ACTIVITY_STATE, 0);
    vmcs_write(VmcsField32::GUEST_INTERRUPTIBILITY_STATE, 0);
    vmcs_write(VmcsFieldXX::GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

    // From Volume 3, Section 26.3.1.1: The IA32_SYSENTER_ESP field and the
    // IA32_SYSENTER_EIP field must each contain a canonical address.
    vmcs_write(VmcsFieldXX::GUEST_IA32_SYSENTER_ESP, 0);
    vmcs_write(VmcsFieldXX::GUEST_IA32_SYSENTER_EIP, 0);

    vmcs_write(VmcsField32::GUEST_IA32_SYSENTER_CS, 0);
    vmcs_write(VmcsFieldXX::GUEST_RSP, 0);

    // From Volume 3, Section 24.4.2: If the “VMCS shadowing” VM-execution
    // control is 1, the VMREAD and VMWRITE instructions access the VMCS
    // referenced by this pointer (see Section 24.10). Otherwise, software
    // should set this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry
    // failures (see Section 26.3.1.5).
    vmcs_write(VmcsField64::LINK_POINTER, LINK_POINTER_INVALIDATE);

    if (x86_feature_test(X86_FEATURE_XSAVE)) {
        // Enable x87 state in guest XCR0.
        vmx_state_.guest_state.xcr0 = X86_XSAVE_STATE_X87;
    }

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

    if (x86_feature_test(X86_FEATURE_XSAVE)) {
        // Save the guest XCR0, and load the host XCR0.
        vmx_state->guest_state.xcr0 = x86_xgetbv(0);
        x86_xsetbv(0, vmx_state->host_state.xcr0);
    }
}

status_t VmcsPerCpu::Enter(const VmcsContext& context, GuestPhysicalAddressSpace* gpas,
                           FifoDispatcher* ctl_fifo) {
    AutoVmcsLoad vmcs_load(&page_);
    // FS is used for thread-local storage — save for this thread.
    vmcs_write(VmcsFieldXX::HOST_FS_BASE, read_msr(X86_MSR_IA32_FS_BASE));
    // CR3 is used to maintain the virtual address space — save for this thread.
    vmcs_write(VmcsFieldXX::HOST_CR3, x86_get_cr3());
    // Kernel GS stores the user-space GS (within the kernel) — as the calling
    // user-space thread may change, save this every time.
    edit_msr_list(&host_msr_page_, 0, X86_MSR_IA32_KERNEL_GS_BASE,
                  read_msr(X86_MSR_IA32_KERNEL_GS_BASE));

    if (x86_feature_test(X86_FEATURE_XSAVE)) {
        // Save the host XCR0, and load the guest XCR0.
        vmx_state_.host_state.xcr0 = x86_xgetbv(0);
        x86_xsetbv(0, vmx_state_.guest_state.xcr0);
    }

    if (!vmx_state_.resume) {
        vmcs_write(VmcsFieldXX::GUEST_RIP, context.ip());
        vmcs_write(VmcsFieldXX::GUEST_CR3, context.cr3());
    }

    status_t status = vmx_enter(&vmx_state_);
    if (status != NO_ERROR) {
        uint64_t error = vmcs_read(VmcsField32::INSTRUCTION_ERROR);
        dprintf(SPEW, "vmlaunch failed: %#" PRIx64 "\n", error);
    } else {
        vmx_state_.resume = true;
        status = vmexit_handler(&vmcs_load, &vmx_state_.guest_state, &local_apic_state_, gpas,
                                ctl_fifo);
    }
    return status;
}

template<typename Out, typename In>
void gpr_copy(Out* out, const In& in) {
    out->rax = in.rax;
    out->rcx = in.rcx;
    out->rdx = in.rdx;
    out->rbx = in.rbx;
    out->rbp = in.rbp;
    out->rsi = in.rsi;
    out->rdi = in.rdi;
    out->r8 = in.r8;
    out->r9 = in.r9;
    out->r10 = in.r10;
    out->r11 = in.r11;
    out->r12 = in.r12;
    out->r13 = in.r13;
    out->r14 = in.r14;
    out->r15 = in.r15;
}

status_t VmcsPerCpu::SetGpr(const mx_guest_gpr_t& guest_gpr) {
    gpr_copy(&vmx_state_.guest_state, guest_gpr);
    return NO_ERROR;
}

status_t VmcsPerCpu::GetGpr(mx_guest_gpr_t* guest_gpr) const {
    gpr_copy(guest_gpr, vmx_state_.guest_state);
    return NO_ERROR;
}

status_t VmcsPerCpu::SetApicMem(mxtl::RefPtr<VmObject> apic_mem) {
    auto get_page = [](void* context, size_t offset, size_t index, paddr_t pa) -> status_t {
        *static_cast<void**>(context) = paddr_to_kvaddr(pa);
        return NO_ERROR;
    };
    local_apic_state_.apic_mem = apic_mem;
    return local_apic_state_.apic_mem->Lookup(0, PAGE_SIZE, kPfFlags, get_page,
                                              &local_apic_state_.apic_addr);
}

static int vmcs_setup(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsPerCpu* per_cpu = context->PerCpu();
    return per_cpu->Setup(context->Pml4Address(), context->ApicAccessAddress(),
                          context->MsrBitmapsAddress());
}

// static
status_t VmcsContext::Create(mxtl::RefPtr<VmObject> phys_mem,
                             mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                             mxtl::unique_ptr<VmcsContext>* context) {
    uint num_cpus = arch_max_num_cpus();

    AllocChecker ac;
    VmcsPerCpu* ctxs = new (&ac) VmcsPerCpu[num_cpus];
    if (!ac.check())
        return ERR_NO_MEMORY;

    mxtl::Array<VmcsPerCpu> cpu_ctxs(ctxs, num_cpus);
    mxtl::unique_ptr<VmcsContext> ctx(new (&ac) VmcsContext(ctl_fifo, mxtl::move(cpu_ctxs)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t status = GuestPhysicalAddressSpace::Create(phys_mem, &ctx->gpas_);
    if (status != NO_ERROR)
        return status;

    // Setup common MSR bitmaps.
    VmxInfo vmx_info;
    status = ctx->msr_bitmaps_page_.Alloc(vmx_info, UINT8_MAX);
    if (status != NO_ERROR)
        return status;

    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_PAT);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_EFER);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_FS_BASE);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_GS_BASE);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_KERNEL_GS_BASE);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_STAR);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_LSTAR);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_FMASK);
    ignore_msr(&ctx->msr_bitmaps_page_, X86_MSR_IA32_TSC_ADJUST);

    // Setup common APIC access.
    status = ctx->apic_address_page_.Alloc(vmx_info, 0);
    if (status != NO_ERROR)
        return status;

    status = ctx->gpas_->MapApicPage(APIC_PHYS_BASE, ctx->apic_address_page_.PhysicalAddress());
    if (status != NO_ERROR)
        return status;

    // We ensure the page containing the IO APIC address is not mapped so that
    // we VM exit with an EPT violation when the guest accesses the page.
    status = ctx->gpas_->UnmapRange(kIoApicPhysBase, PAGE_SIZE);
    if (status != NO_ERROR)
        return status;

    // Setup per-CPU structures.
    status = InitPerCpus(vmx_info, &ctx->per_cpus_);
    if (status != NO_ERROR)
        return status;

    status = percpu_exec(vmcs_setup, ctx.get());
    if (status != NO_ERROR)
        return status;

    *context = mxtl::move(ctx);
    return NO_ERROR;
}

VmcsContext::VmcsContext(mxtl::RefPtr<FifoDispatcher> ctl_fifo, mxtl::Array<VmcsPerCpu> per_cpus)
    : ctl_fifo_(ctl_fifo), per_cpus_(mxtl::move(per_cpus)) {}

static int vmcs_clear(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsPerCpu* per_cpu = context->PerCpu();
    return per_cpu->Clear();
}

VmcsContext::~VmcsContext() {
    __UNUSED status_t status = percpu_exec(vmcs_clear, this);
    DEBUG_ASSERT(status == NO_ERROR);
    status = gpas_->UnmapRange(APIC_PHYS_BASE, PAGE_SIZE);
    DEBUG_ASSERT(status == NO_ERROR);
}

paddr_t VmcsContext::Pml4Address() {
    return gpas_->Pml4Address();
}

paddr_t VmcsContext::ApicAccessAddress() {
    return apic_address_page_.PhysicalAddress();
}

paddr_t VmcsContext::MsrBitmapsAddress() {
    return msr_bitmaps_page_.PhysicalAddress();
}

VmcsPerCpu* VmcsContext::PerCpu() {
    return &per_cpus_[arch_curr_cpu_num()];
}

static int vmcs_enter(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsPerCpu* per_cpu = context->PerCpu();
    if (per_cpu->ShouldResume())
        return ERR_UNAVAILABLE;
    if (!per_cpu->HasApicMem())
        return ERR_BAD_STATE;
    status_t status;
    do {
        status = per_cpu->Enter(*context, context->gpas(), context->ctl_fifo());
    } while (status == NO_ERROR);
    return status;
}

status_t VmcsContext::Enter() {
    if (ip_ == UINTPTR_MAX)
        return ERR_BAD_STATE;
    if (cr3_ == UINTPTR_MAX)
        return ERR_BAD_STATE;
    return percpu_exec(vmcs_enter, this);
}

status_t VmcsContext::MemTrap(vaddr_t guest_paddr, size_t size) {
    return gpas_->UnmapRange(guest_paddr, size);
}

status_t VmcsContext::SetGpr(const mx_guest_gpr_t& guest_gpr) {
    // TODO(abdulla): Update this when we move to an external VCPU model.
    return per_cpus_[0].SetGpr(guest_gpr);
}

status_t VmcsContext::GetGpr(mx_guest_gpr_t* guest_gpr) const {
    // TODO(abdulla): Update this when we move to an external VCPU model.
    return per_cpus_[0].GetGpr(guest_gpr);
}

status_t VmcsContext::SetApicMem(mxtl::RefPtr<VmObject> apic_mem) {
    // TODO(abdulla): Update this when we move to an external VCPU model.
    return per_cpus_[0].SetApicMem(apic_mem);
}

status_t VmcsContext::set_ip(uintptr_t guest_ip) {
    if (guest_ip >= gpas_->size())
        return ERR_INVALID_ARGS;
    ip_ = guest_ip;
    return NO_ERROR;
}

status_t VmcsContext::set_cr3(uintptr_t guest_cr3) {
    if (guest_cr3 >= gpas_->size() - PAGE_SIZE)
        return ERR_INVALID_ARGS;
    cr3_ = guest_cr3;
    return NO_ERROR;
}

status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    // Check that the CPU supports VMX.
    if (!x86_feature_test(X86_FEATURE_VMX))
        return ERR_NOT_SUPPORTED;

    return VmxonContext::Create(context);
}

status_t arch_guest_create(mxtl::RefPtr<VmObject> phys_mem,
                           mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                           mxtl::unique_ptr<GuestContext>* context) {
    return VmcsContext::Create(phys_mem, ctl_fifo, context);
}

status_t arch_guest_enter(const mxtl::unique_ptr<GuestContext>& context) {
    return context->Enter();
}

status_t arch_guest_mem_trap(const mxtl::unique_ptr<GuestContext>& context, vaddr_t guest_paddr,
                             size_t size) {
    return context->MemTrap(guest_paddr, size);
}

status_t arch_guest_set_gpr(const mxtl::unique_ptr<GuestContext>& context,
                            const mx_guest_gpr_t& guest_gpr) {
    return context->SetGpr(guest_gpr);
}

status_t arch_guest_get_gpr(const mxtl::unique_ptr<GuestContext>& context,
                            mx_guest_gpr_t* guest_gpr) {
    return context->GetGpr(guest_gpr);
}

status_t x86_guest_set_apic_mem(const mxtl::unique_ptr<GuestContext>& context,
                                mxtl::RefPtr<VmObject> apic_mem) {
    return context->SetApicMem(apic_mem);
}

status_t arch_guest_set_ip(const mxtl::unique_ptr<GuestContext>& context, uintptr_t guest_ip) {
    return context->set_ip(guest_ip);
}

status_t x86_guest_set_cr3(const mxtl::unique_ptr<GuestContext>& context, uintptr_t guest_cr3) {
    return context->set_cr3(guest_cr3);
}
