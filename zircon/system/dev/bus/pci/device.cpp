// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2018, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "bus.h"
#include "common.h"
#include "device.h"
#include "ref_counted.h"
#include "upstream_node.h"
#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

namespace pci {

namespace { // anon namespace.  Externals do not need to know about DeviceImpl

class DeviceImpl : public Device {
public:
    static zx_status_t Create(fbl::RefPtr<Config>&& cfg,
                              UpstreamNode* upstream,
                              BusLinkInterface* bli);

    // Implement ref counting, do not let derived classes override.
    PCI_IMPLEMENT_REFCOUNTED;

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(DeviceImpl);

protected:
    DeviceImpl(fbl::RefPtr<Config>&& cfg, UpstreamNode* upstream, BusLinkInterface* bli)
        : Device(std::move(cfg), upstream, bli, false) {}
};

zx_status_t DeviceImpl::Create(fbl::RefPtr<Config>&& cfg,
                               UpstreamNode* upstream,
                               BusLinkInterface* bli) {
    fbl::AllocChecker ac;
    auto raw_dev = new (&ac) DeviceImpl(std::move(cfg), upstream, bli);
    if (!ac.check()) {
        pci_errorf("Out of memory attemping to create PCIe device %s.\n", cfg->addr());
        return ZX_ERR_NO_MEMORY;
    }

    auto dev = fbl::AdoptRef(static_cast<Device*>(raw_dev));
    zx_status_t status = raw_dev->Init();
    if (status != ZX_OK) {
        pci_errorf("Failed to initialize PCIe device %s. (res %d)\n", cfg->addr(), status);
        return status;
    }

    bli->LinkDevice(dev);
    return ZX_OK;
}

} // namespace

Device::Device(fbl::RefPtr<Config>&& config,
               UpstreamNode* upstream,
               BusLinkInterface* bli,
               bool is_bridge)
    : is_bridge_(is_bridge),
      cfg_(std::move(config)),
      bar_count_(is_bridge ? PCI_BAR_REGS_PER_BRIDGE : PCI_BAR_REGS_PER_DEVICE),
      upstream_(upstream),
      bli_(bli) {}

Device::~Device() {
    // We should already be unlinked from the bus's device tree.
    ZX_DEBUG_ASSERT(disabled_);
    ZX_DEBUG_ASSERT(!plugged_in_);

    // Make certain that all bus access (MMIO, PIO, Bus mastering) has been
    // disabled.  Also, explicitly disable legacy IRQs. 
    // TODO(cja/ZX-3147)): Only use the PCIe int disable if PCIe
    ModifyCmd(PCI_COMMAND_IO_EN | PCI_COMMAND_MEM_EN, PCIE_CFG_COMMAND_INT_DISABLE);

    // TODO(cja/ZX-3147): Remove this after porting is finished.
    pci_tracef("%s [%s] dtor finished\n", is_bridge() ? "bridge" : "device", cfg_->addr());
}

zx_status_t Device::Create(fbl::RefPtr<Config>&& config,
                           UpstreamNode* upstream,
                           BusLinkInterface* bli) {
    return DeviceImpl::Create(std::move(config), upstream, bli);
}

zx_status_t Device::Init() {
    fbl::AutoLock dev_lock(&dev_lock_);

    zx_status_t status = InitLocked();
    if (status != ZX_OK) {
        pci_errorf("failed to initialize device %s: %d\n", cfg_->addr(), status);
        return status;
    }

    // Things went well and the device is in a good state. Flag the device
    // as plugged in and link ourselves up to the graph.
    upstream_->LinkDevice(this);
    plugged_in_ = true;

    return status;
}

zx_status_t Device::InitLocked() {
    zx_status_t status;

    // Cache basic device info
    vendor_id_ = cfg_->Read(Config::kVendorId);
    device_id_ = cfg_->Read(Config::kDeviceId);
    class_id_ = cfg_->Read(Config::kBaseClass);
    subclass_ = cfg_->Read(Config::kSubClass);
    prog_if_ = cfg_->Read(Config::kProgramInterface);
    rev_id_ = cfg_->Read(Config::kRevisionId);

    // Determine the details of each of the BARs, but do not actually allocate
    // space on the bus for them yet. Allocation will be handled when our upstream
    // bridge or root bring us online.
    if ((status = ProbeBarsLocked()) != ZX_OK) {
        return status;
    }

    // Parse and sanity check the capabilities and extended capabilities lists
    // if they exist
    // TODO(cja): Capability initialization

    // Now that we know what our capabilities are, initialize our internal IRQ
    // bookkeeping
    // TODO(cja): IRQ initialization

    return status;
}

zx_status_t Device::ModifyCmd(uint16_t clr_bits, uint16_t set_bits) {
    fbl::AutoLock dev_lock(&dev_lock_);
    // In order to keep internal bookkeeping coherent, and interactions between
    // MSI/MSI-X and Legacy IRQ mode safe, API users may not directly manipulate
    // the legacy IRQ enable/disable bit.  Just ignore them if they try to
    // manipulate the bit via the modify cmd API.
    // TODO(cja) This only applies to PCI(e)
    clr_bits = static_cast<uint16_t>(clr_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);
    set_bits = static_cast<uint16_t>(set_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);

    if (plugged_in_) {
        ModifyCmdLocked(clr_bits, set_bits);
        return ZX_OK;
    }

    return ZX_ERR_UNAVAILABLE;
}

void Device::ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits) {
    fbl::AutoLock cmd_reg_lock(&cmd_reg_lock_);
    cfg_->Write(
        Config::kCommand,
        static_cast<uint16_t>((cfg_->Read(Config::kCommand) & ~clr_bits) | set_bits));
}

zx_status_t Device::ProbeBarLocked(uint bar_id) {
    ZX_DEBUG_ASSERT(cfg_);
    ZX_DEBUG_ASSERT(bar_id < bar_count_);
    ZX_DEBUG_ASSERT(bar_id < fbl::count_of(bars_));

    // Determine the type of BAR this is. Make sure that it is one of the types
    // we understand.
    BarInfo& bar_info = bars_[bar_id];
    uint32_t bar_val = cfg_->Read(Config::kBar(bar_id));
    bar_info.is_mmio = (bar_val & PCI_BAR_IO_TYPE_MASK) == PCI_BAR_IO_TYPE_MMIO;
    bar_info.is_64bit = bar_info.is_mmio &&
                        ((bar_val & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_TYPE_64BIT);
    bar_info.is_prefetchable = bar_info.is_mmio && (bar_val & PCI_BAR_MMIO_PREFETCH_MASK);
    bar_info.first_bar_reg = bar_id;

    if (bar_info.is_64bit) {
        if ((bar_id + 1) >= bar_count_) {
            pci_errorf("Illegal 64-bit MMIO BAR position (%u/%u) while fetching BAR info "
                       "for device config @%p\n",
                       bar_id, bar_count_, &cfg_);
            return ZX_ERR_BAD_STATE;
        }
    } else {
        if (bar_info.is_mmio &&
            ((bar_val & PCI_BAR_MMIO_TYPE_MASK) != PCI_BAR_MMIO_TYPE_32BIT)) {
            pci_errorf("Unrecognized MMIO BAR type (BAR[%u] == 0x%08x) while fetching"
                       "BAR info for device config @%p\n",
                       bar_id, bar_val, &cfg_);
            return ZX_ERR_BAD_STATE;
        }
    }

    // Disable either MMIO or PIO (depending on the BAR type) access while we
    // perform the probe.  We don't want the addresses written during probing to
    // conflict with anything else on the bus.  Note:  No drivers should have
    // access to this device's registers during the probe process as the device
    // should not have been published yet.  That said, there could be other
    // (special case) parts of the system accessing a devices registers at this
    // point in time, like an early init debug console or serial port.  Don't
    // make any attempt to print or log until the probe operation has been
    // completed.  Hopefully these special systems are quiescent at this point
    // in time, otherwise they might see some minor glitching while access is
    // disabled.
    uint16_t backup = cfg_->Read(Config::kCommand);
    if (bar_info.is_mmio) {
        cfg_->Write(Config::kCommand, static_cast<uint16_t>(backup & ~PCI_COMMAND_MEM_EN));
    } else {
        cfg_->Write(Config::kCommand, static_cast<uint16_t>(backup & ~PCI_COMMAND_IO_EN));
    }

    // Figure out the size of this BAR region by writing 1's to the address
    // bits, then reading back to see which bits the device considers
    // un-configurable.
    uint32_t addr_mask = bar_info.is_mmio ? PCI_BAR_MMIO_ADDR_MASK : PCI_BAR_PIO_ADDR_MASK;
    uint32_t addr_lo = bar_val & addr_mask;
    uint64_t size_mask;

    cfg_->Write(Config::kBar(bar_id), bar_val | addr_mask);
    size_mask = ~(cfg_->Read(Config::kBar(bar_id)) & addr_mask);
    cfg_->Write(Config::kBar(bar_id), bar_val);

    if (bar_info.is_mmio) {
        if (bar_info.is_64bit) {

            // 64bit MMIO? Probe the upper bits as well
            bar_id++;
            bar_val = cfg_->Read(Config::kBar(bar_id));
            cfg_->Write(Config::kBar(bar_id), 0xFFFFFFFF);
            size_mask |= ((uint64_t)~cfg_->Read(Config::kBar(bar_id))) << 32;
            cfg_->Write(Config::kBar(bar_id), bar_val);
            bar_info.size = size_mask + 1;
            bar_info.bus_addr = (static_cast<uint64_t>(bar_val) << 32) | addr_lo;
        } else {
            bar_info.size = (uint32_t)(size_mask + 1);
            bar_info.bus_addr = addr_lo;
        }
    } else {
        // PIO Bar
        bar_info.size = ((uint32_t)(size_mask + 1)) & PCI_PIO_ADDR_SPACE_MASK;
        bar_info.bus_addr = addr_lo;
    }

    // Restore the command register to its previous value
    cfg_->Write(Config::kCommand, backup);
    return ZX_OK;
}

zx_status_t Device::ProbeBarsLocked() {
    __UNUSED uint8_t header_type = cfg_->Read(Config::kHeaderType) & PCI_HEADER_TYPE_MASK;

    ZX_DEBUG_ASSERT((header_type == PCI_HEADER_TYPE_STANDARD) ||
                    (header_type == PCI_HEADER_TYPE_PCI_BRIDGE));
    ZX_DEBUG_ASSERT(bar_count_ <= fbl::count_of(bars_));

    for (uint i = 0; i < bar_count_; ++i) {
        // If this is a re-scan of the bus, We should not be re-enumerating BARs.
        ZX_DEBUG_ASSERT(bars_[i].size == 0);
        ZX_DEBUG_ASSERT(bars_[i].allocation == nullptr);

        zx_status_t probe_res = ProbeBarLocked(i);
        if (probe_res != ZX_OK)
            return probe_res;

        if (bars_[i].size > 0) {
            // If this was a 64 bit bar, it took two registers to store.  Make
            // sure to skip the next register
            if (bars_[i].is_64bit) {
                i++;

                if (i >= bar_count_) {
                    pci_errorf("Device %s claims to have 64-bit BAR in position %u/%u!\n",
                               cfg_->addr(), i, bar_count_);
                    return ZX_ERR_BAD_STATE;
                }
            }
        }
    }

    return ZX_OK;
}

void Device::Disable() {
    fbl::AutoLock dev_lock(&dev_lock_);
    DisableLocked();
}

void Device::DisableLocked() {
    // Disable a device because we cannot allocate space for all of its BARs (or
    // forwarding windows, in the case of a bridge).  Flag the device as
    // disabled from here on out.
    pci_tracef("[%s]%s %s\n", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);

    // Flag the device as disabled.  Close the device's MMIO/PIO windows, shut
    // off device initiated accesses to the bus, disable legacy interrupts.
    // Basically, prevent the device from doing anything from here on out.
    disabled_ = true;
    AssignCmdLocked(PCIE_CFG_COMMAND_INT_DISABLE);

    // Release all BAR allocations back into the pool they came from.
    for (auto& bar : bars_) {
        bar.allocation = nullptr;
    }
}

zx_status_t Device::AllocateBars() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
    return ZX_OK;
}

void Device::Unplug() {
    pci_tracef("[%s]%s %s\n", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);
    // Begin by completely nerfing this device, and preventing an new API
    // operations on it.  We need to be inside the dev lock to do this.  Note:
    // it is assumed that we will not disappear during any of this function,
    // because our caller is holding a reference to us.
    fbl::AutoLock dev_lock(&dev_lock_);
    // Disable should have been called before Unplug and would have disabled
    // everything in the command register
    ZX_DEBUG_ASSERT(disabled_);
    upstream_->UnlinkDevice(this);
    bli_->UnlinkDevice(this);
    plugged_in_ = false;
    pci_tracef("device [%s] unplugged\n", cfg_->addr());
}

void Device::Dump() const {
    printf("PCI: device at %s vid:did %04x:%04x\n", cfg_->addr(), vendor_id(), device_id());
}

} // namespace pci
