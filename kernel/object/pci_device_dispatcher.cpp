// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <object/pci_device_dispatcher.h>

#include <kernel/auto_lock.h>
#include <magenta/rights.h>
#include <object/pci_interrupt_dispatcher.h>
#include <object/process_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

using fbl::AutoLock;

mx_status_t PciDeviceDispatcher::Create(uint32_t                  index,
                                        mx_pcie_device_info_t*    out_info,
                                        fbl::RefPtr<Dispatcher>* out_dispatcher,
                                        mx_rights_t*              out_rights) {
    auto bus_drv = PcieBusDriver::GetDriver();
    if (bus_drv == nullptr)
        return MX_ERR_BAD_STATE;

    auto device = bus_drv->GetNthDevice(index);
    if (device == nullptr)
        return MX_ERR_OUT_OF_RANGE;

    fbl::AllocChecker ac;
    auto disp = new (&ac) PciDeviceDispatcher(fbl::move(device), out_info);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *out_dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    *out_rights     = MX_DEFAULT_PCI_DEVICE_RIGHTS;
    return MX_OK;
}

PciDeviceDispatcher::PciDeviceDispatcher(fbl::RefPtr<PcieDevice> device,
                                         mx_pcie_device_info_t* out_info)
    : device_(device) {

    out_info->vendor_id         = device_->vendor_id();
    out_info->device_id         = device_->device_id();
    out_info->base_class        = device_->class_id();
    out_info->sub_class         = device_->subclass();
    out_info->program_interface = device_->prog_if();
    out_info->revision_id       = device_->rev_id();
    out_info->bus_id            = static_cast<uint8_t>(device_->bus_id());
    out_info->dev_id            = static_cast<uint8_t>(device_->dev_id());
    out_info->func_id           = static_cast<uint8_t>(device_->func_id());
}

PciDeviceDispatcher::~PciDeviceDispatcher() {
    // Bus mastering and IRQ configuration are two states that should be
    // disabled when the driver using them has been unloaded.
    DEBUG_ASSERT(device_);

    mx_status_t s = EnableBusMaster(false);
    if (s != MX_OK) {
        printf("Failed to disable bus mastering on %02x:%02x:%1x\n",
               device_->bus_id(), device_->dev_id(), device_->func_id());
    }

    s = SetIrqMode(static_cast<mx_pci_irq_mode_t>(PCIE_IRQ_MODE_DISABLED), 0);
    if (s != MX_OK) {
        printf("Failed to disable IRQs on %02x:%02x:%1x\n",
               device_->bus_id(), device_->dev_id(), device_->func_id());
    }

    // Release our reference to the underlying PCI device state to indicate that
    // we are now closed.
    //
    // Note: we should not need the lock at this point in time.  We are
    // destructing, if there are any other threads interacting with methods in
    // this object, then we have a serious lifecycle management problem.

    device_ = nullptr;
}

mx_status_t PciDeviceDispatcher::EnableBusMaster(bool enable) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    device_->EnableBusMaster(enable);

    return MX_OK;
}

mx_status_t PciDeviceDispatcher::EnablePio(bool enable) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    device_->EnablePio(enable);

    return MX_OK;
}

mx_status_t PciDeviceDispatcher::EnableMmio(bool enable) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_ && device_);

    device_->EnableMmio(enable);

    return MX_OK;
}

const pcie_bar_info_t* PciDeviceDispatcher::GetBar(uint32_t bar_num) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    return device_->GetBarInfo(bar_num);
}

mx_status_t PciDeviceDispatcher::GetConfig(pci_config_info_t* out) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    if (!out) {
        return MX_ERR_INVALID_ARGS;
    }

    auto cfg = device_->config();
    out->size = (device_->is_pcie()) ? PCIE_EXTENDED_CONFIG_SIZE : PCIE_BASE_CONFIG_SIZE;
    out->base_addr = cfg->base();
    out->is_mmio = (cfg->addr_space() == PciAddrSpace::MMIO);

    return MX_OK;
}

mx_status_t PciDeviceDispatcher::ResetDevice() {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    return device_->DoFunctionLevelReset();
}

mx_status_t PciDeviceDispatcher::MapInterrupt(int32_t which_irq,
                                              fbl::RefPtr<Dispatcher>* interrupt_dispatcher,
                                              mx_rights_t* rights) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    if ((which_irq < 0) ||
        (static_cast<uint32_t>(which_irq) >= irqs_avail_cnt_))
        return MX_ERR_INVALID_ARGS;

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
mx_status_t PciDeviceDispatcher::QueryIrqModeCaps(mx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    pcie_irq_mode_caps_t caps;
    mx_status_t ret = device_->QueryIrqModeCapabilities(static_cast<pcie_irq_mode_t>(mode),
                                                        &caps);

    *out_max_irqs = (ret == MX_OK) ? caps.max_irqs : 0;
    return ret;
}

mx_status_t PciDeviceDispatcher::SetIrqMode(mx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(device_);

    if (mode == MX_PCIE_IRQ_MODE_DISABLED)
        requested_irq_count = 0;

    mx_status_t ret;
    ret = device_->SetIrqMode(static_cast<pcie_irq_mode_t>(mode), requested_irq_count);
    if (ret == MX_OK) {
        pcie_irq_mode_caps_t caps;
        ret = device_->QueryIrqModeCapabilities(static_cast<pcie_irq_mode_t>(mode), &caps);

        // The only way for QueryIrqMode to fail at this point should be for the
        // device to have become unplugged.
        if (ret == MX_OK) {
            irqs_avail_cnt_ = requested_irq_count;
            irqs_maskable_  = caps.per_vector_masking_supported;
        } else {
            device_->SetIrqMode(PCIE_IRQ_MODE_DISABLED, 0);
            irqs_avail_cnt_ = 0;
            irqs_maskable_  = false;
        }
    }

    return ret;
}

#endif  // if WITH_DEV_PCIE
