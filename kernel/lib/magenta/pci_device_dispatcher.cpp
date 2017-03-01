// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <kernel/auto_lock.h>
#include <lib/user_copy.h>
#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_interrupt_dispatcher.h>
#include <magenta/pci_io_mapping_dispatcher.h>
#include <magenta/process_dispatcher.h>

#include <mxalloc/new.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

constexpr mx_rights_t kDefaultPciDeviceRights =
    MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;

status_t PciDeviceDispatcher::Create(uint32_t                  index,
                                     mx_pcie_device_info_t*    out_info,
                                     mxtl::RefPtr<Dispatcher>* out_dispatcher,
                                     mx_rights_t*              out_rights) {
    status_t status;
    mxtl::RefPtr<PciDeviceWrapper> device_wrapper;

    status = PciDeviceWrapper::Create(index, &device_wrapper);
    if (status != NO_ERROR)
        return status;

    DEBUG_ASSERT(device_wrapper);

    AllocChecker ac;
    auto disp = new (&ac) PciDeviceDispatcher(device_wrapper, out_info);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *out_dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    *out_rights     = kDefaultPciDeviceRights;
    return NO_ERROR;
}

PciDeviceDispatcher::PciDeviceDispatcher(mxtl::RefPtr<PciDeviceWrapper> device,
                                         mx_pcie_device_info_t* out_info)
    : device_(device) {
    const PcieDevice& dev = *device_->device();

    out_info->vendor_id         = dev.vendor_id();
    out_info->device_id         = dev.device_id();
    out_info->base_class        = dev.class_id();
    out_info->sub_class         = dev.subclass();
    out_info->program_interface = dev.prog_if();
    out_info->revision_id       = dev.rev_id();
    out_info->bus_id            = static_cast<uint8_t>(dev.bus_id());
    out_info->dev_id            = static_cast<uint8_t>(dev.dev_id());
    out_info->func_id           = static_cast<uint8_t>(dev.func_id());
}

PciDeviceDispatcher::~PciDeviceDispatcher() {
    // Release our reference to the underlying PCI device state to indicate that
    // we are now closed.
    //
    // Note: we should not need the lock at this point in time.  We are
    // destructing, if there are any other threads interacting with methods in
    // this object, then we have a serious lifecycle management problem.
    device_ = nullptr;
}

status_t PciDeviceDispatcher::ClaimDevice() {
    canary_.Assert();

    status_t result;
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (device_->claimed()) return ERR_ALREADY_BOUND;  // Are we claimed already?

    result = device_->Claim();
    if (result != NO_ERROR)
        return result;

    return NO_ERROR;
}

status_t PciDeviceDispatcher::EnableBusMaster(bool enable) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?

    device_->device()->EnableBusMaster(enable);

    return NO_ERROR;
}

status_t PciDeviceDispatcher::EnablePio(bool enable) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?

    device_->device()->EnablePio(enable);

    return NO_ERROR;
}

status_t PciDeviceDispatcher::EnableMmio(bool enable) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?

    device_->device()->EnableMmio(enable);

    return NO_ERROR;
}

const pcie_bar_info_t* PciDeviceDispatcher::GetBar(uint32_t bar_num) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) return nullptr;  // Are we not claimed yet?

    return device_->device()->GetBarInfo(bar_num);
}

status_t PciDeviceDispatcher::GetConfig(pci_config_info_t* out) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) {
        return ERR_BAD_STATE;
    }

    if (!out) {
        return ERR_INVALID_ARGS;
    }

    auto dev = device_->device();
    auto cfg = dev->config();
    out->size = (dev->is_pcie()) ? PCIE_EXTENDED_CONFIG_SIZE : PCIE_BASE_CONFIG_SIZE;
    out->base_addr = cfg->base();
    out->is_mmio = (cfg->addr_space() == PciAddrSpace::MMIO);

    if (out->is_mmio) {
        out->vmo = dev->config_vmo();
    }

    return NO_ERROR;
}

status_t PciDeviceDispatcher::ResetDevice() {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?

    return device_->device()->DoFunctionLevelReset();
}

status_t PciDeviceDispatcher::MapConfig(mxtl::RefPtr<Dispatcher>* out_mapping,
                                        mx_rights_t* out_rights) {
    canary_.Assert();

    AutoLock lock(&lock_);
    return PciIoMappingDispatcher::Create(device_,
                                         "cfg",
                                          device_->device()->config_phys(),
                                          PCIE_EXTENDED_CONFIG_SIZE,
                                          0 /* vmm flags */,
                                          ARCH_MMU_FLAG_UNCACHED_DEVICE |
                                          ARCH_MMU_FLAG_PERM_READ       |
                                          ARCH_MMU_FLAG_PERM_USER,
                                          out_mapping,
                                          out_rights);
}

status_t PciDeviceDispatcher::MapMmio(uint32_t bar_num,
                                      uint32_t cache_policy,
                                      mxtl::RefPtr<Dispatcher>* out_mapping,
                                      mx_rights_t* out_rights) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

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
        device_->device()->EnableMmio(true);

    return status;
}

status_t PciDeviceDispatcher::MapInterrupt(int32_t which_irq,
                                           mxtl::RefPtr<Dispatcher>* interrupt_dispatcher,
                                           mx_rights_t* rights) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    if (!device_->claimed()) return ERR_BAD_STATE;  // Are we not claimed yet?
    if ((which_irq < 0) ||
        (static_cast<uint32_t>(which_irq) >= irqs_avail_cnt_)) return ERR_INVALID_ARGS;

    // Attempt to create the dispatcher.  It will take care of things like checking for
    // duplicate registration.
    return PciInterruptDispatcher::Create(device_,
                                          which_irq,
                                          irqs_maskable_,
                                          rights,
                                          interrupt_dispatcher);
}

static_assert(static_cast<uint>(MX_PCIE_IRQ_MODE_DISABLED) ==
              static_cast<uint>(PCIE_IRQ_MODE_DISABLED),
              "Mode mismatch, MX_PCIE_IRQ_MODE_DISABLED != PCIE_IRQ_MODE_DISABLED");
static_assert(static_cast<uint>(MX_PCIE_IRQ_MODE_LEGACY) ==
              static_cast<uint>(PCIE_IRQ_MODE_LEGACY),
              "Mode mismatch, MX_PCIE_IRQ_MODE_LEGACY != PCIE_IRQ_MODE_LEGACY");
static_assert(static_cast<uint>(MX_PCIE_IRQ_MODE_MSI) ==
              static_cast<uint>(PCIE_IRQ_MODE_MSI),
              "Mode mismatch, MX_PCIE_IRQ_MODE_MSI != PCIE_IRQ_MODE_MSI");
static_assert(static_cast<uint>(MX_PCIE_IRQ_MODE_MSI_X) ==
              static_cast<uint>(PCIE_IRQ_MODE_MSI_X),
              "Mode mismatch, MX_PCIE_IRQ_MODE_MSI_X != PCIE_IRQ_MODE_MSI_X");
status_t PciDeviceDispatcher::QueryIrqModeCaps(mx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    pcie_irq_mode_caps_t caps;
    status_t ret = device_->device()->QueryIrqModeCapabilities(static_cast<pcie_irq_mode_t>(mode),
                                                               &caps);

    *out_max_irqs = (ret == NO_ERROR) ? caps.max_irqs : 0;
    return ret;
}

status_t PciDeviceDispatcher::SetIrqMode(mx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_->device());

    // Are we not claimed yet?
    if (!device_->claimed())
        return ERR_BAD_STATE;

    if (mode == MX_PCIE_IRQ_MODE_DISABLED)
        requested_irq_count = 0;

    status_t ret;
    ret = device_->device()->SetIrqMode(static_cast<pcie_irq_mode_t>(mode),
                                        requested_irq_count);
    if (ret == NO_ERROR) {
        pcie_irq_mode_caps_t caps;
        ret = device_->device()->QueryIrqModeCapabilities(static_cast<pcie_irq_mode_t>(mode),
                                                          &caps);

        // The only way for QueryIrqMode to fail at this point should be for the
        // device to have become unplugged.
        if (ret == NO_ERROR) {
            irqs_avail_cnt_ = requested_irq_count;
            irqs_maskable_  = caps.per_vector_masking_supported;
        } else {
            device_->device()->SetIrqMode(PCIE_IRQ_MODE_DISABLED, 0);
            irqs_avail_cnt_ = 0;
            irqs_maskable_  = false;
        }
    }

    return ret;
}

PciDeviceDispatcher::PciDeviceWrapper::PciDeviceWrapper(
        mxtl::RefPtr<PcieDevice>&& device)
    : device_(mxtl::move(device)) {
    DEBUG_ASSERT(device_);
    memset(&cp_refs_, 0, sizeof(cp_refs_));
}

PciDeviceDispatcher::PciDeviceWrapper::~PciDeviceWrapper() {
    DEBUG_ASSERT(device_);
    device_->Unclaim();
    device_ = nullptr;
}

status_t PciDeviceDispatcher::PciDeviceWrapper::Claim() {
    if (claimed_)
        return ERR_ALREADY_BOUND;

    status_t result = device_->Claim();
    if (result != NO_ERROR)
        return result;

    claimed_ = true;

    return NO_ERROR;
}

status_t PciDeviceDispatcher::PciDeviceWrapper::Create(
        uint32_t index,
        mxtl::RefPtr<PciDeviceWrapper>* out_device) {
    if (!out_device)
        return ERR_INVALID_ARGS;

    auto bus_drv = PcieBusDriver::GetDriver();
    if (bus_drv == nullptr)
        return ERR_BAD_STATE;

    auto device = bus_drv->GetNthDevice(index);
    if (device == nullptr)
        return ERR_OUT_OF_RANGE;

    AllocChecker ac;
    *out_device = mxtl::AdoptRef<PciDeviceWrapper>(new (&ac) PciDeviceWrapper(mxtl::move(device)));
    return ac.check() ? NO_ERROR : ERR_NO_MEMORY;
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

#endif  // if WITH_DEV_PCIE
