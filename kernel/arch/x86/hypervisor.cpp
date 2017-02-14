// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <new.h>

#include <arch/defines.h>
#include <arch/hypervisor.h>
#include <arch/x86/feature.h>
#include <arch/x86/hypervisor.h>
#include <kernel/mp.h>
#include <magenta/errors.h>

#define X86_MSR_IA32_FEATURE_CONTROL                0x003a /* feature control */
#define X86_MSR_IA32_VMX_BASIC                      0x0480 /* basic info */

/* X86_MSR_IA32_FEATURE_CONTROL bits */
#define X86_MSR_IA32_FEATURE_CONTROL_LOCK           0x01    /* lock bit */
#define X86_MSR_IA32_FEATURE_CONTROL_ENABLE_VMXON   0x04    /* enable VMXON */

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
        // NOTE(abdulla): From Volume 3, Appendix A.1: "Bits 44:32 report the
        // number of bytes that software should allocate for the VMXON region
        // and any VMCS region. It is a value greater than 0 and at most 4096
        // (bit 44 is set if and only if bits 43:32 are clear)."
        DEBUG_ASSERT(region_size < PAGE_SIZE);
        memory_type = static_cast<uint8_t>(BITS_SHIFT(basic_info, 53, 50));
        ins_outs = static_cast<bool>(BIT_SHIFT(basic_info, 54));
        vmx_controls = static_cast<bool>(BIT_SHIFT(basic_info, 55));
    }
};

/* VMX region to be used with both VMXON and VMCS. */
struct VmxRegion {
    uint32_t revision_id;
} __PACKED;

class VmxCpuContext {
public:
    ~VmxCpuContext() {
        if (vmxon_page_ != nullptr)
            pmm_free_page(vmxon_page_);
    }

    mx_status_t Alloc(const VmxInfo& info) {
        // The maximum size for a VMXON region is 4096, therefore
        // unconditionally allocating a page is adequate.
        vmxon_page_ = pmm_alloc_page(0, &vmxon_pa_);
        if (vmxon_page_ == nullptr)
            return ERR_NO_MEMORY;

        vmxon_region_ = static_cast<VmxRegion*>(paddr_to_kvaddr(vmxon_pa_));
        vmxon_region_->revision_id = info.revision_id;
        return NO_ERROR;
    }

    void VmxOn() {
        status_ = vmxon(vmxon_pa_);
    }

    void VmxOff() {
        status_ = vmxoff();
    }

    mx_status_t status() { return status_; }
    void set_status(mx_status_t status) { status_ = status; }

private:
    mx_status_t status_;
    paddr_t vmxon_pa_;
    vm_page_t* vmxon_page_ = nullptr;
    VmxRegion* vmxon_region_;
};

static void vmx_enable(void* arg) {
    VmxContext* context = static_cast<VmxContext*>(arg);
    VmxCpuContext* cpu_context = context->CurrCpuContext();

    // Enable VMXON, if required.
    uint64_t feature_control = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
    if (!(feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) ||
        !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_ENABLE_VMXON)) {
        if ((feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) &&
            !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_ENABLE_VMXON)) {
            cpu_context->set_status(ERR_NOT_SUPPORTED);
            return;
        }
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_LOCK;
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_ENABLE_VMXON;
        write_msr(X86_MSR_IA32_FEATURE_CONTROL, feature_control);
    }

    // Enable VMX.
    x86_set_cr4(x86_get_cr4() | X86_CR4_VMXE);

    // Execute VMXON.
    cpu_context->VmxOn();
}

// static
mx_status_t VmxContext::Create(mxtl::unique_ptr<VmxContext>* context) {
    uint num_cpus = arch_max_num_cpus();

    AllocChecker ac;
    VmxCpuContext* ctxs = new (&ac) VmxCpuContext[num_cpus];
    if (!ac.check())
        return ERR_NO_MEMORY;

    mxtl::Array<VmxCpuContext> cpu_ctxs(ctxs, num_cpus);
    mxtl::unique_ptr<VmxContext> vmx_ctx(new (&ac) VmxContext(mxtl::move(cpu_ctxs)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    VmxInfo info;
    mx_status_t status = vmx_ctx->AllocCpuContexts(info);
    if (status != NO_ERROR)
        return status;

    mp_sync_exec(MP_CPU_ALL, vmx_enable, vmx_ctx.get());

    status = vmx_ctx->CpuContextStatus();
    if (status != NO_ERROR)
        return status;

    *context = mxtl::move(vmx_ctx);
    return NO_ERROR;
}

VmxContext::VmxContext(mxtl::Array<VmxCpuContext> cpu_contexts)
    : cpu_contexts_(mxtl::move(cpu_contexts)) {}

mx_status_t VmxContext::AllocCpuContexts(const VmxInfo& info) {
    for (size_t i = 0; i < cpu_contexts_.size(); i++) {
        mx_status_t status = cpu_contexts_[i].Alloc(info);
        if (status != NO_ERROR)
            return status;
    }
    return NO_ERROR;
}

mx_status_t VmxContext::CpuContextStatus() {
    for (size_t i = 0; i < cpu_contexts_.size(); i++) {
        if (cpu_contexts_[i].status() != NO_ERROR)
            return cpu_contexts_[i].status();
    }
    return NO_ERROR;
}

static void vmx_disable(void* arg) {
    VmxContext* context = static_cast<VmxContext*>(arg);

    // Execute VMXOFF.
    context->CurrCpuContext()->VmxOff();

    // Disable VMX.
    x86_set_cr4(x86_get_cr4() & ~X86_CR4_VMXE);
}

VmxContext::~VmxContext() {
    mp_sync_exec(MP_CPU_ALL, vmx_disable, this);
    DEBUG_ASSERT(CpuContextStatus() == NO_ERROR);
}

VmxCpuContext* VmxContext::CurrCpuContext() {
    return &cpu_contexts_[arch_curr_cpu_num()];
}

mx_status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    // Check that the CPU supports VMX.
    if (!x86_feature_test(X86_FEATURE_VMX))
        return ERR_NOT_SUPPORTED;

    return VmxContext::Create(context);
}
