// Copyright 2019 The Fuchsia Authors
// Copyright (c) 2019, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include "config.h"
#include "ref_counted.h"
#include <assert.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <hw/pci.h>
#include <region-alloc/region-alloc.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>

namespace pci {

// UpstreamNode includes device.h, so only forward declare it here.
class UpstreamNode;

// struct used to fetch information about a configured base address register
struct BarInfo {
    uint64_t size = 0;
    uint64_t bus_addr = 0;
    bool is_mmio;
    bool is_64bit;
    bool is_prefetchable;
    uint32_t first_bar_reg;
    RegionAllocator::Region::UPtr allocation;
};

// A Device represents a given PCI(e) device on a bus. It can be used standalone
// for a regular PCI(e) device on the bus, or as the base class for a Bridge.
// Most work a Device does is limited to its own registers in configuration space
// and are managed through their Config object handled to it during creation.
class Device : public fbl::DoublyLinkedListable<Device*> {
public:
    // Create, but do not initialize, a device.
    static zx_status_t Create(fbl::RefPtr<Config>&& config, UpstreamNode* upstream);
    virtual ~Device();

    // Bridge or DeviceImpl will need to implement refcounting
    PCI_REQUIRE_REFCOUNTED;

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(Device);

    // Trigger a function level reset (if possible)
    // TODO(cja): port zx_status_t DoFunctionLevelReset() when we have a way to test it

    // Modify bits in the device's command register (in the device config space),
    // clearing the bits specified by clr_bits and setting the bits specified by set
    // bits.  Specifically, the operation will be applied as...
    //
    // WR(cmd, (RD(cmd) & ~clr) | set)
    //
    // @param clr_bits The mask of bits to be cleared.
    // @param clr_bits The mask of bits to be set.
    // @return A zx_status_t indicating success or failure of the operation.
    zx_status_t ModifyCmd(uint16_t clr_bits, uint16_t set_bits);

    // Enable or disable bus mastering in a device's configuration.
    //
    // @param enable If true, allow the device to access main system memory as a bus
    // master.
    // @return A zx_status_t indicating success or failure of the operation.
    inline zx_status_t EnableBusMaster(bool enabled) {
        if (enabled && disabled_) {
            return ZX_ERR_BAD_STATE;
        }

        return ModifyCmd(enabled ? 0 : PCI_COMMAND_BUS_MASTER_EN,
                         enabled ? PCI_COMMAND_BUS_MASTER_EN : 0);
    }

    // Enable or disable PIO access in a device's configuration.
    //
    // @param enable If true, allow the device to access its PIO mapped registers.
    // @return A zx_status_t indicating success or failure of the operation.
    inline zx_status_t EnablePio(bool enabled) {
        if (enabled && disabled_) {
            return ZX_ERR_BAD_STATE;
        }

        return ModifyCmd(enabled ? 0 : PCI_COMMAND_IO_EN,
                         enabled ? PCI_COMMAND_IO_EN : 0);
    }

    // Enable or disable MMIO access in a device's configuration.
    //
    // @param enable If true, allow the device to access its MMIO mapped registers.
    // @return A zx_status_t indicating success or failure of the operation.
    inline zx_status_t EnableMmio(bool enabled) {
        if (enabled && disabled_) {
            return ZX_ERR_BAD_STATE;
        }

        return ModifyCmd(enabled ? 0 : PCI_COMMAND_MEM_EN,
                         enabled ? PCI_COMMAND_MEM_EN : 0);
    }

    // Return information about the requested base address register, if it has been
    // allocated.  Otherwise, return NULL.
    //
    // @param bar_ndx The index of the BAR register to fetch info for.
    //
    // @return A pointer to the BAR info, including where in the bus address space
    // the BAR window has been mapped, or NULL if the BAR window does not exist or
    // has not been allocated.
    const BarInfo* GetBarInfo(uint32_t bar_ndx) const {
        if (bar_ndx >= bar_count_) {
            return nullptr;
        }

        ZX_DEBUG_ASSERT(bar_ndx < fbl::count_of(bars_));

        const BarInfo* ret = &bars_[bar_ndx];
        return (!disabled_ && (ret->allocation != nullptr)) ? ret : nullptr;
    }

    virtual void Unplug();
    void SetQuirksDone() { quirks_done_ = true; }
    const fbl::RefPtr<Config>& config() const { return cfg_; }

    bool plugged_in() const { return plugged_in_; }
    bool disabled() const { return disabled_; }
    bool quirks_done() const { return quirks_done_; }

    bool is_bridge() const { return is_bridge_; }
    uint16_t vendor_id() const { return vendor_id_; }
    uint16_t device_id() const { return device_id_; }
    uint8_t class_id() const { return class_id_; }
    uint8_t subclass() const { return subclass_; }
    uint8_t prog_if() const { return prog_if_; }
    uint8_t rev_id() const { return rev_id_; }

    uint8_t bus_id() const {
        ZX_ASSERT(cfg_);
        return cfg_->bdf().bus_id;
    }

    uint8_t dev_id() const {
        ZX_ASSERT(cfg_);
        return cfg_->bdf().device_id;
    }

    uint8_t func_id() const {
        ZX_ASSERT(cfg_);
        return cfg_->bdf().function_id;
    }
    uint32_t bar_count() const { return bar_count_; }

    // Dump some information about the device
    virtual void Dump() const;

    // Devices need to exist in both the top level bus driver class, as well
    // as in a list for roots/bridges to track their downstream children. These
    // traits facilitate that for us.
    using NodeState = fbl::WAVLTreeNodeState<fbl::RefPtr<pci::Device>>;
    struct BusListTraits {
        static NodeState& node_state(Device& device) {
            return device.bus_list_state_;
        }
    };

    // These traits are used for the WAVL tree implementation. They allow device objects
    // to be sorted and found in trees by composite bdf address.
    struct KeyTraitsSortByBdf {
        static const pci_bdf_t& GetKey(pci::Device& dev) {
            return dev.cfg_->bdf();
        }

        static bool LessThan(const pci_bdf_t& bdf1, const pci_bdf_t& bdf2) {
            return (bdf1.bus_id < bdf2.bus_id) ||
                   ((bdf1.bus_id == bdf2.bus_id) && (bdf1.device_id < bdf2.device_id)) ||
                   ((bdf1.bus_id == bdf2.bus_id) && (bdf1.device_id == bdf2.device_id) &&
                    (bdf1.function_id < bdf2.function_id));
        }

        static bool EqualTo(const pci_bdf_t& bdf1, const pci_bdf_t& bdf2) {
            return (bdf1.bus_id == bdf2.bus_id) && (bdf1.device_id == bdf2.device_id) &&
                   (bdf1.function_id == bdf2.function_id);
        }
    };

protected:
    // Allow our upstream to disable / Unplug us
    friend class UpstreamNode;
    Device(fbl::RefPtr<Config>&& config, UpstreamNode* upstream, bool is_bridge);
    zx_status_t Init();
    zx_status_t InitLocked();
    fbl::Mutex* dev_lock() { return &dev_lock_; }

    void ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits);
    void AssignCmdLocked(uint16_t value) { ModifyCmdLocked(0xFFFF, value); }

    zx_status_t ProbeBarsLocked();
    zx_status_t ProbeBarLocked(uint32_t bar_id);
    // TODO(cja): port zx_status_t ProbeCapabilitiesLocked();
    // TODO(cja): port zx_status_t ParseStdCapabilitiesLocked();
    // TODO(cja): port zx_status_t ParseExtCapabilitiesLocked();

    // BAR allocation
    virtual zx_status_t AllocateBars();
    // TODO(cja): port zx_status_t AllocateBarsLocked();
    // TODO(cja): port zx_status_t AllocateBarLocked(BarInfo& info);

    // Disable a device, and anything downstream of it.  The device will
    // continue to enumerate, but users will only be able to access config (and
    // only in a read only fashion).  BAR windows, bus mastering, and interrupts
    // will all be disabled.
    virtual void Disable();
    void DisableLocked();

    fbl::Mutex cmd_reg_lock_;       // Protection for access to the command register.
    const bool is_bridge_;          // True if this device is also a bridge
    const fbl::RefPtr<Config> cfg_; // Pointer to the device's config interface.
    uint16_t vendor_id_;            // The device's vendor ID, as read from config
    uint16_t device_id_;            // The device's device ID, as read from config
    uint8_t class_id_;              // The device's class ID, as read from config.
    uint8_t subclass_;              // The device's subclass, as read from config.
    uint8_t prog_if_;               // The device's programming interface (from cfg)
    uint8_t rev_id_;                // The device's revision ID (from cfg)

    /* State related to lifetime management */
    mutable fbl::Mutex dev_lock_;
    bool plugged_in_ = false;
    bool disabled_ = false;
    bool quirks_done_ = false;

    // Info about the BARs computed and cached during the initial setup/probe,
    // indexed by starting BAR register index.
    BarInfo bars_[PCI_MAX_BAR_REGS];
    const uint32_t bar_count_;

    // An upstream node will outlive its downstream devices
    UpstreamNode* upstream_; // The upstream node in the device graph.
private:
    // Capabilities
    // TODO(cja): Port over the capability support from kernel pci.

    // IRQ structures
    // TODO(cja): Port over the IRQ support from kernel pci.

    // These allow a device to exist in an upstream node list as well
    // as the top level bus list of all devices.
    friend struct BusListTraits;
    friend struct UpstreamListTraits;
    NodeState bus_list_state_;
};

} // namespace pci
