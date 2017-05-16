// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_io_mapping_dispatcher.h>

#include <mxalloc/new.h>

#include <string.h>

status_t PciIoMappingDispatcher::Create(
        const mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device,
        const char* dbg_tag,
        paddr_t paddr,
        size_t size,
        uint vmm_flags,
        uint arch_mmu_flags,
        mxtl::RefPtr<Dispatcher>* out_dispatcher,
        mx_rights_t* out_rights) {
    // Sanity check our args
    if (!device || !out_rights || !out_dispatcher)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    // Attempt to allocate a new dispatcher wrapper.
    PciIoMappingDispatcher* pim_disp = new (&ac) PciIoMappingDispatcher(device);
    if (!ac.check())
        return ERR_NO_MEMORY;

    // Create a debug name for the mapping.
    char name[32];
    const PcieDevice& dev = *device->device();
    snprintf(name, sizeof(name), "usr_pci_%s_%02x_%02x_%x",
             dbg_tag, dev.bus_id(), dev.dev_id(), dev.func_id());

    // Initialize the mapping
    mxtl::RefPtr<Dispatcher> disp = mxtl::AdoptRef<Dispatcher>(pim_disp);
    status_t status = pim_disp->Init(name, paddr, size, vmm_flags, arch_mmu_flags);
    if (status != NO_ERROR) {
        pim_disp->Close();
        return status;
    }

    // Success!  Stash the results.
    *out_dispatcher = mxtl::move(disp);
    *out_rights     = IoMappingDispatcher::kDefaultRights;
    return NO_ERROR;
}

status_t PciIoMappingDispatcher::CreateBarMapping(
        const mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device,
        uint bar_num,
        uint vmm_flags,
        uint cache_policy,
        mxtl::RefPtr<Dispatcher>* out_dispatcher,
        mx_rights_t* out_rights) {
    // Sanity check our args
    if (!device || !out_rights || !out_dispatcher)
        return ERR_INVALID_ARGS;

    if (!device->device())
        return ERR_BAD_STATE;

    // Fetch our BAR info.
    const pcie_bar_info_t* info = device->device()->GetBarInfo(bar_num);
    if (!info) return ERR_INVALID_ARGS;

    // Fail if this is a PIO windows instead of an MMIO window.  For the time
    // being, PIO accesses need to go through the specialized PIO API.
    if (!info->is_mmio) return ERR_INVALID_ARGS;

    // Caller only gets to control the cache policy, nothing else.
    if (cache_policy & ~ARCH_MMU_FLAG_CACHE_MASK) return ERR_INVALID_ARGS;

    // check to make sure the 64bit values will fit in arch-sized physical addresses and size
    if (info->bus_addr > ULONG_MAX) return ERR_OUT_OF_RANGE;
    if (info->size     > SIZE_MAX)  return ERR_OUT_OF_RANGE;

    // Attempt to mark this BAR as being mapped with the specified cache policy.
    // While mapping a BAR multiple times is allowed, the cache policy must
    // match each time.
    status_t status = device->AddBarCachePolicyRef(bar_num, cache_policy);
    if (status != NO_ERROR)
        return status;

    // Create our debug tag
    char tag[32];
    snprintf(tag, sizeof(tag), "bar%u", bar_num);

    // make sure the BAR mapping is at least a page. The pcie driver should already have
    // allocated BAR addresses on a page boundary.
    uint64_t aligned_size = ROUNDUP(info->size, PAGE_SIZE);

    // Go ahead and create the I/O Mapping object.  If this fails, remember to
    // release our cache policy reference.
    status = Create(device,
                    tag,
                    static_cast<paddr_t>(info->bus_addr),
                    static_cast<size_t>(aligned_size),
                    0 /* vmm flags */,
                    cache_policy |
                    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                        ARCH_MMU_FLAG_PERM_USER,
                    out_dispatcher,
                    out_rights);

    if (status != NO_ERROR) {
        device->ReleaseBarCachePolicyRef(bar_num);
        return status;
    }

    // Success, record our BAR number in our I/O mapping object so that it will
    // release its cache mode reference when it becomes closed.
    DEBUG_ASSERT(out_dispatcher);
    static_cast<PciIoMappingDispatcher*>(out_dispatcher->get())->bar_num_ = bar_num;
    return NO_ERROR;
}

PciIoMappingDispatcher::~PciIoMappingDispatcher() {
    // Just ASSERT that we have been closed already.
    DEBUG_ASSERT(device_ == nullptr);
}

void PciIoMappingDispatcher::Close() {
    // Close the underlying mapping; this will unmap the range from the user's process.
    IoMappingDispatcher::Close();

    // Now that we are unmapped, if this was a BAR mapping, be sure to release
    // the reference to the cache policy we were holding.
    if (bar_num_ < PCIE_MAX_BAR_REGS) {
        device_->ReleaseBarCachePolicyRef(bar_num_);
        bar_num_  = PCIE_MAX_BAR_REGS;
    }

    device_ = nullptr;
}

#endif  // if WITH_DEV_PCIE
