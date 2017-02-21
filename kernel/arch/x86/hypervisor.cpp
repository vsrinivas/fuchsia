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
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <magenta/errors.h>

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

extern uint8_t _gdt[];
extern uint8_t _idt[];

static mx_status_t vmxon(paddr_t pa) {
    uint8_t err;

    __asm__ (
        "vmxon %[pa];"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static mx_status_t vmxoff() {
    uint8_t err;

    __asm__ (
        "vmxoff;"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err)
        :
        : "cc");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static mx_status_t vmptrld(paddr_t pa) {
    uint8_t err;

    __asm__ (
        "vmptrld %[pa];"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static mx_status_t vmclear(paddr_t pa) {
    uint8_t err;

    __asm__ (
        "vmclear %[pa];"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static uint64_t vmread(uint64_t field) {
    uint8_t err;
    uint64_t val = 0;

    __asm__ (
        "vmread %[val], %[field];"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err), [val] "=r"(val)
        : [field] "r"(field)
        : "cc", "memory");

    DEBUG_ASSERT(err == NO_ERROR);
    return val;
}

static void vmwrite(uint64_t field, uint64_t val) {
    uint8_t err;

    __asm__ (
        "vmwrite %[val], %[field];"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err)
        : [val] "r"(val), [field] "r"(field)
        : "cc", "memory");

    DEBUG_ASSERT(err == NO_ERROR);
}

static mx_status_t vmlaunch() {
    uint8_t err;

    __asm__ (
        "vmlaunch;"
        "setna %[err];"     // Check CF and ZF for error.
        : [err] "=r"(err)
        :
        : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

// TODO(abdulla): Update this to execute on every CPU. For development, it is
// convenient to only consider a single CPU for now.
mx_status_t percpu_exec(thread_start_routine entry, void* arg) {
    thread_t *t = thread_create("vmx", entry, arg, HIGH_PRIORITY, DEFAULT_STACK_SIZE);
    if (!t)
        return ERR_NO_MEMORY;

    thread_set_pinned_cpu(t, 0);
    mx_status_t status = thread_resume(t);
    if (status != NO_ERROR)
        return status;

    int retcode;
    status = thread_join(t, &retcode, INFINITE_TIME);
    return status != NO_ERROR ? status : retcode;
}

struct VmxInfo {
    uint32_t revision_id;
    uint16_t region_size;
    uint8_t memory_type;
    bool ins_outs;
    bool vmx_controls;

    VmxInfo() {
        uint64_t basic_info = read_msr(X86_MSR_IA32_VMX_BASIC);
        revision_id = static_cast<uint32_t>(BITS(basic_info, 30, 0));
        region_size = static_cast<uint16_t>(BITS_SHIFT(basic_info, 44, 32));
        memory_type = static_cast<uint8_t>(BITS_SHIFT(basic_info, 53, 50));
        ins_outs = static_cast<bool>(BIT_SHIFT(basic_info, 54));
        vmx_controls = static_cast<bool>(BIT_SHIFT(basic_info, 55));
    }
};

/* VMX region to be used with both VMXON and VMCS. */
struct VmxRegion {
    uint32_t revision_id;
};

class VmxCpuContext {
public:
    virtual ~VmxCpuContext() {}

    virtual mx_status_t Init(const VmxInfo& info) {
        mx_status_t status = page_.Alloc(info);
        if (status != NO_ERROR)
            return status;

        VmxRegion* region = static_cast<VmxRegion*>(page_.VirtualAddress());
        region->revision_id = info.revision_id;
        return NO_ERROR;
    }

protected:
    VmxPage page_;
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

class VmxonCpuContext : public VmxCpuContext {
public:
    mx_status_t VmxOn() {
        return vmxon(page_.PhysicalAddress());
    }

    mx_status_t VmxOff() {
        return vmxoff();
    }
};

VmxPage::~VmxPage() {
    if (page_ != nullptr)
        pmm_free_page(page_);
}

mx_status_t VmxPage::Alloc(const VmxInfo& info) {
    // From Volume 3, Appendix A.1: Bits 44:32 report the number of bytes that
    // software should allocate for the VMXON region and any VMCS region. It is
    // a value greater than 0 and at most 4096 (bit 44 is set if and only if
    // bits 43:32 are clear).
    if (info.region_size > PAGE_SIZE)
        return ERR_NO_MEMORY;

    // Ensure we use write back memory, as recommended.
    if (info.memory_type != VMX_MEMORY_TYPE_WRITE_BACK)
        return ERR_NOT_SUPPORTED;

    // The maximum size for a VMXON or VMCS region is 4096, therefore
    // unconditionally allocating a page is adequate.
    page_ = pmm_alloc_page(0, &pa_);
    if (page_ == nullptr)
        return ERR_NO_MEMORY;

    memset(VirtualAddress(), 0, PAGE_SIZE);
    return NO_ERROR;
}

void* VmxPage::VirtualAddress() {
    return paddr_to_kvaddr(pa_);
}

static int vmx_enable(void* arg) {
    VmxonContext* context = static_cast<VmxonContext*>(arg);
    VmxonCpuContext* cpu_context = context->CurrCpuContext();

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

    // Setup control registers for VMX.
    uint64_t cr0_mask = read_msr(X86_MSR_IA32_VMX_CR0_FIXED0) |
                        read_msr(X86_MSR_IA32_VMX_CR0_FIXED1);
    uint64_t cr0 = x86_get_cr0();
    DEBUG_ASSERT(cr0 == (cr0 & cr0_mask));
    x86_set_cr0(cr0 & cr0_mask);

    uint64_t cr4_mask = read_msr(X86_MSR_IA32_VMX_CR4_FIXED0) |
                        read_msr(X86_MSR_IA32_VMX_CR4_FIXED1);
    uint64_t cr4 = x86_get_cr4();
    DEBUG_ASSERT(cr4 == (cr4 & cr4_mask));
    x86_set_cr4((cr4 & cr4_mask) | X86_CR4_VMXE);

    // Execute VMXON.
    return cpu_context->VmxOn();
}

// static
mx_status_t VmxonContext::Create(mxtl::unique_ptr<VmxonContext>* context) {
    uint num_cpus = arch_max_num_cpus();

    AllocChecker ac;
    VmxonCpuContext* ctxs = new (&ac) VmxonCpuContext[num_cpus];
    if (!ac.check())
        return ERR_NO_MEMORY;

    mxtl::Array<VmxonCpuContext> cpu_ctxs(ctxs, num_cpus);
    mxtl::unique_ptr<VmxonContext> ctx(new (&ac) VmxonContext(mxtl::move(cpu_ctxs)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    mx_status_t status = InitCpuContexts(&ctx->cpu_contexts_);
    if (status != NO_ERROR)
        return status;

    status = percpu_exec(vmx_enable, ctx.get());
    if (status != NO_ERROR)
        return status;

    *context = mxtl::move(ctx);
    return NO_ERROR;
}

VmxonContext::VmxonContext(mxtl::Array<VmxonCpuContext> cpu_contexts)
    : cpu_contexts_(mxtl::move(cpu_contexts)) {}

static int vmx_disable(void* arg) {
    VmxonContext* context = static_cast<VmxonContext*>(arg);
    VmxonCpuContext* cpu_context = context->CurrCpuContext();

    // Execute VMXOFF.
    mx_status_t status = cpu_context->VmxOff();
    if (status != NO_ERROR)
        return status;

    // Disable VMX.
    x86_set_cr4(x86_get_cr4() & ~X86_CR4_VMXE);
    return NO_ERROR;
}

VmxonContext::~VmxonContext() {
    DEBUG_ASSERT(percpu_exec(vmx_disable, this) == NO_ERROR);
}

VmxonCpuContext* VmxonContext::CurrCpuContext() {
    return &cpu_contexts_[arch_curr_cpu_num()];
}

static void set_vmcs_control(uint32_t control, uint64_t state, uint32_t set) {
    uint32_t allowed_0 = static_cast<uint32_t>(BITS(state, 31, 0));
    uint32_t allowed_1 = static_cast<uint32_t>(BITS_SHIFT(state, 63, 31));
    uint32_t value = allowed_0 | (allowed_1 & set);
    vmwrite(control, value);
}

class VmcsCpuContext : public VmxCpuContext {
public:
    mx_status_t Init(const VmxInfo& info) override {
        mx_status_t status = VmxCpuContext::Init(info);
        if (status != NO_ERROR)
            return status;

        return msr_bitmaps_page_.Alloc(info);
    }

    mx_status_t Setup(const VmxInfo& info) {
        // Execute VMCLEAR.
        mx_status_t status = vmclear(page_.PhysicalAddress());
        if (status != NO_ERROR)
            return status;

        // Execute VMPTRLD.
        status = vmptrld(page_.PhysicalAddress());
        if (status != NO_ERROR)
            return status;

        // From Volume 3, Section 24.4.2: If the “VMCS shadowing” VM-execution
        // control is 1, the VMREAD and VMWRITE instructions access the VMCS
        // referenced by this pointer (see Section 24.10). Otherwise, software
        // should set this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry
        // failures (see Section 26.3.1.5).
        vmwrite(VMCS_64_LINK_POINTER, VMCS_64_LINK_POINTER_INVALIDATE);

        // From Volume 3, Section 24.6.3: The exception bitmap is a 32-bit field
        // that contains one bit for each exception. When an exception occurs,
        // its vector is used to select a bit in this field. If the bit is 1,
        // the exception causes a VM exit. If the bit is 0, the exception is
        // delivered normally through the IDT, using the descriptor
        // corresponding to the exception’s vector.
        vmwrite(VMCS_32_EXCEPTION_BITMAP, VMCS_32_EXCEPTION_BITMAP_ALL_EXCEPTIONS);

        // We only support full VMX controls.
        if (!info.vmx_controls)
            return ERR_NOT_SUPPORTED;

        // Setup VMCS controls.
        set_vmcs_control(VMCS_32_PROCBASED_CTLS2,
                         // Secondary processor-based controls.
                         read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                         // Enable use of RDTSCP instruction.
                         VMCS_32_PROCBASED_CTLS2_RDTSCP |
                         // Enable use of XSAVES and XRSTORS instructions.
                         VMCS_32_PROCBASED_CTLS2_XSAVES_XRSTORS);
        set_vmcs_control(VMCS_32_PINBASED_CTLS,
                         // Pin-based controls.
                         read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS),
                         // External interrupts cause a VM exit.
                         VMCS_32_PINBASED_CTLS_EXTINT_EXITING |
                         // Non-maskable interrupts cause a VM exit.
                         VMCS_32_PINBASED_CTLS_NMI_EXITING);
        set_vmcs_control(VMCS_32_PROCBASED_CTLS,
                         // Primary processor-based controls.
                         read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS),
                         // Enable secondary processor-based controls.
                         VMCS_32_PROCBASED_CTLS_PROCBASED_CTLS2);
        set_vmcs_control(VMCS_32_EXIT_CTLS,
                         // VM-exit controls.
                         read_msr(X86_MSR_IA32_VMX_TRUE_EXIT_CTLS),
                         // Logical processor is in 64-bit mode after VM exit.
                         // On VM exit CS.L, IA32_EFER.LME, and IA32_EFER.LMA is
                         // set to true.
                         VMCS_32_EXIT_CTLS_64BIT_MODE |
                         // On VM exit due to an external interrupt, make the
                         // logical processor acknowledge the interrupt
                         // controller, acquiring the interrupt’s vector.
                         VMCS_32_EXIT_CTLS_ACK_INTERRUPT |
                         VMCS_32_EXIT_CTLS_LOAD_IA32_PAT |
                         VMCS_32_EXIT_CTLS_LOAD_IA32_EFER);
        set_vmcs_control(VMCS_32_ENTRY_CTLS,
                         // VM-entry controls.
                         read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                         // Logical processor is in IA-32e mode after VM entry.
                         // On VM entry IA32_EFER.LMA is set to true.
                         VMCS_32_ENTRY_CTLS_IA32E_MODE);

        // Setup VMCS host state.
        vmwrite(VMCS_16_HOST_CS_SELECTOR, CODE_64_SELECTOR);
        vmwrite(VMCS_16_HOST_TR_SELECTOR, TSS_SELECTOR(arch_curr_cpu_num()));
        vmwrite(VMCS_64_HOST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
        vmwrite(VMCS_64_HOST_IA32_EFER, read_msr(X86_MSR_EFER));
        vmwrite(VMCS_XX_HOST_FS_BASE, read_msr(X86_MSR_IA32_FS_BASE));
        vmwrite(VMCS_XX_HOST_GS_BASE, read_msr(X86_MSR_IA32_GS_BASE));
        vmwrite(VMCS_XX_HOST_TR_BASE, reinterpret_cast<uint64_t>(&x86_get_percpu()->default_tss));
        vmwrite(VMCS_XX_HOST_GDTR_BASE, reinterpret_cast<uint64_t>(_gdt));
        vmwrite(VMCS_XX_HOST_IDTR_BASE, reinterpret_cast<uint64_t>(_idt));
        vmwrite(VMCS_XX_HOST_CR0, x86_get_cr0());
        vmwrite(VMCS_XX_HOST_CR4, x86_get_cr4());

        return NO_ERROR;
    }

private:
    VmxPage msr_bitmaps_page_;
};

static int vmcs_setup(void* arg) {
    VmcsContext* context = static_cast<VmcsContext*>(arg);
    VmcsCpuContext* cpu_context = context->CurrCpuContext();

    VmxInfo info;
    return cpu_context->Setup(info);
}

// static
mx_status_t VmcsContext::Create(mxtl::unique_ptr<VmcsContext>* context) {
    uint num_cpus = arch_max_num_cpus();

    AllocChecker ac;
    VmcsCpuContext* ctxs = new (&ac) VmcsCpuContext[num_cpus];
    if (!ac.check())
        return ERR_NO_MEMORY;

    mxtl::Array<VmcsCpuContext> cpu_ctxs(ctxs, num_cpus);
    mxtl::unique_ptr<VmcsContext> ctx(new (&ac) VmcsContext(mxtl::move(cpu_ctxs)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    mx_status_t status = InitCpuContexts(&ctx->cpu_contexts_);
    if (status != NO_ERROR)
        return status;

    status = percpu_exec(vmcs_setup, ctx.get());
    if (status != NO_ERROR)
        return status;

    *context = mxtl::move(ctx);
    return NO_ERROR;
}

VmcsContext::VmcsContext(mxtl::Array<VmcsCpuContext> cpu_contexts)
    : cpu_contexts_(mxtl::move(cpu_contexts)) {}

VmcsContext::~VmcsContext() {}

VmcsCpuContext* VmcsContext::CurrCpuContext() {
    return &cpu_contexts_[arch_curr_cpu_num()];
}

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

static int vmcs_launch(void* arg) {
    VmxHostState host_state;
    if (vmx_host_save(&host_state)) {
        uint64_t reason = vmread(VMCS_32_EXIT_REASON);
        dprintf(SPEW, "vmexit reason: %#" PRIx64 "\n", reason);
        return NO_ERROR;
    }

    vmwrite(VMCS_XX_HOST_RIP, reinterpret_cast<uint64_t>(vmx_host_load));
    vmwrite(VMCS_XX_HOST_RSP, reinterpret_cast<uint64_t>(&host_state));
    vmwrite(VMCS_XX_HOST_CR3, x86_get_cr3());

    mx_status_t status = vmlaunch();
    if (status != NO_ERROR) {
        uint64_t error = vmread(VMCS_32_INSTRUCTION_ERROR);
        dprintf(SPEW, "vmlaunch failed: %#" PRIx64 "\n", error);
    }
    return status;
}

mx_status_t VmcsContext::Start(uintptr_t entry, uintptr_t stack) {
    return percpu_exec(vmcs_launch, nullptr);
}

mx_status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    // Check that the CPU supports VMX.
    if (!x86_feature_test(X86_FEATURE_VMX))
        return ERR_NOT_SUPPORTED;

    return VmxonContext::Create(context);
}

mx_status_t arch_guest_create(mxtl::unique_ptr<GuestContext>* context) {
    return VmcsContext::Create(context);
}

mx_status_t arch_guest_start(GuestContext* context, uintptr_t entry, uintptr_t stack) {
    return context->Start(entry, stack);
}
