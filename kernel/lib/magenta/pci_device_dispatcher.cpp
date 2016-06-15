// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/auto_lock.h>
#include <lib/user_copy.h>
#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_interrupt_dispatcher.h>
#include <magenta/pci_io_mapping_dispatcher.h>
#include <magenta/user_process.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

constexpr mx_rights_t kDefaultPciDeviceRights =
    MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;

static const pcie_driver_fn_table_t PCIE_FN_TABLE = {
    .pcie_probe_fn    = nullptr,
    .pcie_startup_fn  = nullptr,
    .pcie_shutdown_fn = nullptr,
    .pcie_release_fn  = nullptr,
};

static const pcie_driver_registration_t PCIE_DRIVER_REGISTRATION = {
    .name = "userspace PCI driver", .fn_table = &PCIE_FN_TABLE,
};

status_t PciDeviceDispatcher::Create(uint32_t                   index,
                                     mx_pcie_get_nth_info_t*    out_info,
                                     utils::RefPtr<Dispatcher>* out_dispatcher,
                                     mx_rights_t*               out_rights) {
    pcie_device_state_t* device = pci_get_nth_device(index);
    if (!device)
        return ERR_OUT_OF_RANGE;

    status_t status;
    utils::RefPtr<PciDeviceWrapper> device_wrapper;

    status = PciDeviceWrapper::Create(device, &device_wrapper);
    if (status != NO_ERROR)
        return status;

    DEBUG_ASSERT(device_wrapper);
    auto disp = new PciDeviceDispatcher(device_wrapper, out_info);
    if (!disp)
        return ERR_NO_MEMORY;

    *out_dispatcher = utils::AdoptRef<Dispatcher>(disp);
    *out_rights     = kDefaultPciDeviceRights;
    return NO_ERROR;
}

PciDeviceDispatcher::PciDeviceDispatcher(utils::RefPtr<PciDeviceWrapper> device,
                                         mx_pcie_get_nth_info_t* out_info)
    : device_(device) {
    mutex_init(&lock_);

    const pcie_common_state_t& common = device_->device()->common;
    const pcie_config_t* cfg = common.cfg;
    DEBUG_ASSERT(cfg);

    out_info->vendor_id         = pcie_read16(&cfg->base.vendor_id);
    out_info->device_id         = pcie_read16(&cfg->base.device_id);
    out_info->base_class        = pcie_read8 (&cfg->base.base_class);
    out_info->sub_class         = pcie_read8 (&cfg->base.sub_class);
    out_info->program_interface = pcie_read8 (&cfg->base.program_interface);
    out_info->revision_id       = pcie_read8 (&cfg->base.revision_id_0);
    out_info->bus_id            = static_cast<uint8_t>(common.bus_id);
    out_info->dev_id            = static_cast<uint8_t>(common.dev_id);
    out_info->func_id           = static_cast<uint8_t>(common.func_id);
}

PciDeviceDispatcher::~PciDeviceDispatcher() {
    // By the time that we destruct, we should have been formally closed.
    DEBUG_ASSERT(!device_);
}

void PciDeviceDispatcher::Close(Handle* handle) {
    // Release our reference to the underlying PCI device state to indicate that
    // we are now closed.
    AutoLock lock(&lock_);
    device_ = nullptr;
}

status_t PciDeviceDispatcher::ClaimDevice() {
    status_t result;
    AutoLock lock(&lock_);

    if (!device_) return ERR_BAD_HANDLE;      // Are we closed already?
    if (device_->claimed()) return ERR_BUSY;  // Are we claimed already?

    // TODO(johngro) : Lifetime management issues regarding interactions between
    // PCIe bus driver level objects and this dispatcher need to be addressed.
    // Specifially, if a device were to spontaniously shutdown (hot-unplug
    // event) and call back through the driver shutdown hook, Bad Things could
    // happen if there was a user mode thread currently attempting to interact
    // with the device.  See Bug #MG-65
    result = device_->Claim();
    if (result != NO_ERROR)
        return result;

    // TODO(johngro) : Move the process of negotiating the IRQ mode, allocation
    // and sharing disposition up to the user-mode driver.
    //
    // Query the device to figure out which IRQ modes it supports, then pick a
    // mode and claim our vectors.
    pcie_irq_mode_caps_t mode_caps;

    // Does this device support MSI?  If so, choose this as the mode and attempt
    // to grab as many vectors as we can get away with.
    result = pcie_query_irq_mode_capabilities(&device_->device()->common,
                                              PCIE_IRQ_MODE_MSI,
                                              &mode_caps);
    if (result == NO_ERROR) {
        while (mode_caps.max_irqs) {
            result = pcie_set_irq_mode(&device_->device()->common,
                                       PCIE_IRQ_MODE_MSI,
                                       mode_caps.max_irqs,
                                       PCIE_IRQ_SHARE_MODE_EXCLUSIVE);

            if (result == NO_ERROR) {
                irqs_supported_ = mode_caps.max_irqs;
                irqs_maskable_  = mode_caps.per_vector_masking_supported;
                break;
            }

            mode_caps.max_irqs >>= 1;
        }
    }

    // If MSI didn't end up working out for us, try for Legacy mode.
    if (!irqs_supported_) {
        result = pcie_set_irq_mode(&device_->device()->common,
                                   PCIE_IRQ_MODE_LEGACY,
                                   1,
                                   PCIE_IRQ_SHARE_MODE_SYSTEM_SHARED);
        if (result == NO_ERROR) {
            irqs_supported_ = 1;
            irqs_maskable_  = true;
        }
    }

    return NO_ERROR;
}

status_t PciDeviceDispatcher::EnableBusMaster(bool enable) {
    AutoLock lock(&lock_);

    if (!device_) return ERR_BAD_HANDLE;            // Are we closed already?
    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?

    pcie_enable_bus_master(device_->device(), enable);

    return NO_ERROR;
}

status_t PciDeviceDispatcher::ResetDevice() {
    // TODO(johngro) : Either remove this from the API, or implement it as function level reset.
    return ERR_NOT_IMPLEMENTED;
}

status_t PciDeviceDispatcher::MapConfig(utils::RefPtr<Dispatcher>* out_mapping,
                                        mx_rights_t* out_rights) {
    AutoLock lock(&lock_);
    return PciIoMappingDispatcher::Create(device_,
                                         "cfg",
                                          device_->device()->common.cfg_phys,
                                          PCIE_EXTENDED_CONFIG_SIZE,
                                          0 /* vmm flags */,
                                          ARCH_MMU_FLAG_UNCACHED_DEVICE |
                                          ARCH_MMU_FLAG_PERM_RO         |
                                          ARCH_MMU_FLAG_PERM_NO_EXECUTE |
                                          ARCH_MMU_FLAG_PERM_USER,
                                          out_mapping,
                                          out_rights);
}

status_t PciDeviceDispatcher::MapMmio(uint32_t bar_num,
                                      uint32_t cache_policy,
                                      utils::RefPtr<Dispatcher>* out_mapping,
                                      mx_rights_t* out_rights) {
    AutoLock lock(&lock_);
    if (!device_) return ERR_BAD_HANDLE;           // Are we closed already?
    if (!device_->claimed()) return ERR_BAD_STATE; // Are we not claimed yet?

    status_t status;
    status = PciIoMappingDispatcher::CreateBarMapping(device_,
                                                     bar_num,
                                                     0 /* vmm flags */,
                                                     cache_policy,
                                                     out_mapping,
                                                     out_rights);

    // If things went well, make sure that mmio is turned on
    if (status == NO_ERROR)
        pcie_enable_mmio(device_->device(), true);

    return status;
}

status_t PciDeviceDispatcher::MapInterrupt(int32_t which_irq,
                                           utils::RefPtr<Dispatcher>* interrupt_dispatcher,
                                           mx_rights_t* rights) {
    AutoLock lock(&lock_);

    if (!device_) return ERR_BAD_HANDLE;            // Are we closed already?
    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?
    if ((which_irq < 0) ||
        (static_cast<uint32_t>(which_irq) >= irqs_supported_)) return ERR_INVALID_ARGS;

    // Attempt to create the dispatcher.  It will take care of things like checking for
    // duplicate registration.
    return PciInterruptDispatcher::Create(device_,
                                          which_irq,
                                          irqs_maskable_,
                                          rights,
                                          interrupt_dispatcher);
}

PciDeviceDispatcher::PciDeviceWrapper::PciDeviceWrapper(pcie_device_state_t* device)
    : device_(device) {
    DEBUG_ASSERT(device_);
    mutex_init(&cp_ref_lock_);
    memset(&cp_refs_, 0, sizeof(cp_refs_));
}

PciDeviceDispatcher::PciDeviceWrapper::~PciDeviceWrapper() {
    DEBUG_ASSERT(device_);
    if (claimed_)
        pcie_shutdown_device(device_);
}

status_t PciDeviceDispatcher::PciDeviceWrapper::Claim() {
    if (claimed_)
        return ERR_BUSY;

    status_t result = pcie_claim_and_start_device(device_, &PCIE_DRIVER_REGISTRATION, NULL);
    if (result != NO_ERROR)
        return result;

    claimed_ = true;

    return NO_ERROR;
}

status_t PciDeviceDispatcher::PciDeviceWrapper::Create(
        pcie_device_state_t* device,
        utils::RefPtr<PciDeviceWrapper>* out_device) {
    if (!device || !out_device)
        return ERR_INVALID_ARGS;

    *out_device = utils::AdoptRef<PciDeviceWrapper>(new PciDeviceWrapper(device));

    return *out_device ? NO_ERROR : ERR_NO_MEMORY;
}

status_t PciDeviceDispatcher::PciDeviceWrapper::AddBarCachePolicyRef(uint bar_num,
                                                                     uint32_t cache_policy) {
    AutoLock lock(&cp_ref_lock_);

    if (bar_num >= countof(cp_refs_))
        return ERR_INVALID_ARGS;

    CachePolicyRef& r = cp_refs_[bar_num];
    if (r.ref_count && (r.cache_policy != cache_policy))
        return ERR_BAD_STATE;

    r.ref_count++;
    r.cache_policy = cache_policy;

    return NO_ERROR;
}

void PciDeviceDispatcher::PciDeviceWrapper::ReleaseBarCachePolicyRef(uint bar_num) {
    AutoLock lock(&cp_ref_lock_);

    CachePolicyRef& r = cp_refs_[bar_num];
    DEBUG_ASSERT(bar_num < countof(cp_refs_));
    DEBUG_ASSERT(r.ref_count);

    r.ref_count--;
}
