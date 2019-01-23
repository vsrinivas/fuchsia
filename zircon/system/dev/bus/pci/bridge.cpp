// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "bridge.h"
#include "bus.h"
#include "common.h"
#include "config.h"
#include <assert.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>
#include <inttypes.h>
#include <string.h>
#include <zircon/compiler.h>

namespace pci {

// Bridges rely on most of the protected Device members when they can
Bridge::Bridge(fbl::RefPtr<Config>&& config, UpstreamNode* upstream, uint8_t mbus_id)
    : pci::Device(std::move(config), upstream, true),
      UpstreamNode(UpstreamNode::Type::BRIDGE, mbus_id) {
    /* Assign the driver-wide region pool to this bridge's allocators. */
}

zx_status_t Bridge::Create(fbl::RefPtr<Config>&& config,
                           UpstreamNode* upstream,
                           uint8_t mbus_id,
                           fbl::RefPtr<pci::Bridge>* out_bridge) {
    fbl::AllocChecker ac;
    auto raw_bridge = new (&ac) Bridge(std::move(config), upstream, mbus_id);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = raw_bridge->Init();
    if (status != ZX_OK) {
        delete raw_bridge;
        return status;
    }

    fbl::RefPtr<pci::Device> dev = fbl::AdoptRef(raw_bridge);
    Bus::LinkDeviceToBus(dev);
    *out_bridge = fbl::RefPtr<Bridge>::Downcast(dev);
    return ZX_OK;
}

zx_status_t Bridge::Init() {
    fbl::AutoLock dev_lock(&dev_lock_);

    // Initialize the device portion of ourselves first. This will handle initializing
    // bars/capabilities, and linking ourselves upstream before we need the information
    // for our own window allocation.
    zx_status_t status = pci::Device::InitLocked();
    if (status != ZX_OK) {
        return status;
    }

    // Sanity checks of bus allocation.
    //
    // TODO(johngro) : Strengthen sanity checks around bridge topology and
    // handle the need to reconfigure bridge topology if a bridge happens to be
    // misconfigured.  Right now, we just assume that the BIOS/Bootloader has
    // taken care of bridge configuration.  In the short term, it would be good
    // to add some protection against cycles in the bridge configuration which
    // could lead to infinite recursion.
    uint8_t primary_id = cfg_->Read(Config::kPrimaryBusId);
    uint8_t secondary_id = cfg_->Read(Config::kSecondaryBusId);

    if (primary_id == secondary_id) {
        pci_errorf("PCI-to-PCI bridge detected at %s claims to be bridged to itsef "
                   "(primary %02x == secondary %02x)... skipping scan.\n",
                   cfg_->addr(), primary_id, secondary_id);
        return ZX_ERR_BAD_STATE;
    }

    if (primary_id != cfg_->bdf().bus_id) {
        pci_errorf("PCI-to-PCI bridge detected at %s has invalid primary bus id "
                   "(%02x)... skipping scan.\n",
                   cfg_->addr(), primary_id);
        return ZX_ERR_BAD_STATE;
    }

    if (secondary_id != managed_bus_id()) {
        pci_errorf("PCI-to-PCI bridge detected at %s has invalid secondary bus id "
                   "(%02x)... skipping scan.\n",
                   cfg_->addr(), secondary_id);
        return ZX_ERR_BAD_STATE;
    }

    // Parse the state of its I/O and Memory windows.
    status = ParseBusWindowsLocked();
    if (status != ZX_OK) {
        return status;
    }

    // Things went well and the device is in a good state. Add ourself to the upstream
    // graph and mark as plugged in.
    upstream_->LinkDevice(static_cast<pci::Device*>(this));
    plugged_in_ = true;

    // Release the device lock, then recurse and scan for downstream devices.
    return ZX_OK;
}

zx_status_t Bridge::ParseBusWindowsLocked() {
    // Parse the currently configured windows used to determine MMIO/PIO
    // forwarding policy for this bridge.
    //
    // See The PCI-to-PCI Bridge Architecture Specification Revision 1.2,
    // section 3.2.5 and chapter 4 for detail.
    uint32_t base, limit;

    // I/O window
    base = cfg_->Read(Config::kIoBase);
    limit = cfg_->Read(Config::kIoLimit);

    supports_32bit_pio_ = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit & 0xF));
    io_base_ = (base & ~0xF) << 8;
    io_limit_ = (limit << 8) | 0xFFF;
    if (supports_32bit_pio_) {
        io_base_ |= static_cast<uint32_t>(cfg_->Read(Config::kIoBaseUpper)) << 16;
        io_limit_ |= static_cast<uint32_t>(cfg_->Read(Config::kIoLimitUpper)) << 16;
    }

    // Non-prefetchable memory window
    mem_base_ = (static_cast<uint32_t>(cfg_->Read(Config::kMemoryBase)) << 16) & ~0xFFFFF;
    mem_limit_ = (static_cast<uint32_t>(cfg_->Read(Config::kMemoryLimit)) << 16) | 0xFFFFF;

    // Prefetchable memory window
    base = cfg_->Read(Config::kPrefetchableMemoryBase);
    limit = cfg_->Read(Config::kPrefetchableMemoryLimit);

    bool supports_64bit_pf_mem = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit & 0xF));
    pf_mem_base_ = (base & ~0xF) << 16;
    pf_mem_limit_ = (limit << 16) | 0xFFFFF;
    if (supports_64bit_pf_mem) {
        pf_mem_base_ |=
            static_cast<uint64_t>(cfg_->Read(Config::kPrefetchableMemoryBaseUpper)) << 32;
        pf_mem_limit_ |=
            static_cast<uint64_t>(cfg_->Read(Config::kPrefetchableMemoryLimitUpper)) << 32;
    }

    return ZX_OK;
}

void Bridge::Dump() const {
    pci::Device::Dump();

    printf("\tbridge managed bus id %#02x\n", managed_bus_id());
    printf("\tio base %#x limit %#x\n", io_base(), io_limit());
    printf("\tmem base %#x limit %#x\n", mem_base(), mem_limit());
    printf("\tprefectable base %#" PRIx64 " limit %#" PRIx64 "\n", pf_mem_base(), pf_mem_limit());
}

void Bridge::Unplug() {
    UnplugDownstream();
    pci::Device::Unplug();
    pci_infof("bridge [%s] unplugged\n", cfg_->addr());
}

zx_status_t Bridge::AllocateBars() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
    return ZX_OK;
}

zx_status_t Bridge::AllocateBridgeWindowsLocked() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
    return ZX_OK;
}

void Bridge::Disable() {
    // Immediately enter the device lock and enter the disabled state.  We want
    // to be outside of the device lock as we disable our downstream devices,
    // but we don't want any new devices to be able to plug into us as we do so.
    {
        fbl::AutoLock dev_lock(&dev_lock_);
        disabled_ = true;
    }

    // Start by disabling all of our downstream devices.  This should prevent
    // them from bothering us moving forward.  Do not hold the device lock while
    // we do this.
    DisableDownstream();

    // Enter the device lock again and finish shooting ourselves in the head.
    {
        fbl::AutoLock dev_lock(&dev_lock_);

        // Disable the device portion of ourselves.
        Device::DisableLocked();

        // Close all of our IO windows at the HW level and update the internal
        // bookkeeping to indicate that they are closed.
        cfg_->Write(Config::kIoBase, 0xF0);
        cfg_->Write(Config::kIoLimit, 0);
        cfg_->Write(Config::kIoBaseUpper, 0);
        cfg_->Write(Config::kIoLimitUpper, 0);

        cfg_->Write(Config::kMemoryBase, 0xFFF0);
        cfg_->Write(Config::kMemoryLimit, 0);

        cfg_->Write(Config::kPrefetchableMemoryBase, 0xFFF0);
        cfg_->Write(Config::kPrefetchableMemoryLimit, 0);
        cfg_->Write(Config::kPrefetchableMemoryBaseUpper, 0);
        cfg_->Write(Config::kPrefetchableMemoryLimitUpper, 0);

        pf_mem_limit_ = mem_limit_ = io_limit_ = 0u;
        pf_mem_base_ = mem_base_ = io_base_ = 1u;

        // Release our internal bookkeeping
        // TODO(cja): Free bookkeeping bits here (they're owned by upstream node, but should
        // be dealt with here.
    }
}

} // namespace pci
