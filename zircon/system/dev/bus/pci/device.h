// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_DEVICE_H_

#include "allocation.h"
#include "capabilities.h"
#include "config.h"
#include "ref_counted.h"
#include <assert.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pci.h>
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
#include <zircon/thread_annotations.h>

namespace pci {

// UpstreamNode includes device.h, so only forward declare it here.
class UpstreamNode;
class BusLinkInterface;

// struct used to fetch information about a configured base address register
struct BarInfo {
    size_t size = 0;
    zx_paddr_t address = 0; // Allocated address for the bar
    bool is_mmio;
    bool is_64bit;
    bool is_prefetchable;
    uint32_t bar_id; // The bar index in the config space. If the bar is 64 bit
                     // then this corresponds to the first half of the register pair
    fbl::unique_ptr<PciAllocation> allocation;
};

// A pci::Device represents a given PCI(e) device on a bus. It can be used
// standalone for a regular PCI(e) device on the bus, or as the base class for a
// Bridge. Most work a pci::Device does is limited to its own registers in
// configuration space and are managed through their Config object handled to it
// during creation. One of the biggest responsibilities of the pci::Device class
// is fulfill the PCI protocol for the driver downstream operating the PCI
// device this corresponds to.
class Device;
using PciDeviceType = ddk::Device<pci::Device>;
class Device : public PciDeviceType,
               public ddk::PciProtocol<pci::Device>,
               public fbl::DoublyLinkedListable<Device*> {
public:
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

    struct Capabilities {
        CapabilityList list;
        PciExpressCapability* pcie;
    };

    // DDKTL PciProtocol methods
    zx_status_t PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_res);
    zx_status_t PciEnableBusMaster(bool enable);
    zx_status_t PciResetDevice();
    zx_status_t PciMapInterrupt(zx_status_t which_irq, zx::interrupt* out_handle);
    zx_status_t PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs);
    zx_status_t PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count);
    zx_status_t PciGetDeviceInfo(zx_pcie_device_info_t* out_into);
    zx_status_t PciConfigRead(uint16_t offset, size_t width, uint32_t* out_value);
    zx_status_t PciConfigWrite(uint16_t offset, size_t width, uint32_t value);
    uint8_t PciGetNextCapability(uint8_t type, uint8_t offset);
    zx_status_t PciGetAuxdata(const char* args,
                              void* out_data_buffer,
                              size_t data_size,
                              size_t* out_data_actual);
    zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti);

    // DDK mix-in impls
    void DdkRelease() { delete this; }

    // Create, but do not initialize, a device.
    static zx_status_t Create(zx_device_t* parent,
                              fbl::RefPtr<Config>&& config,
                              UpstreamNode* upstream,
                              BusLinkInterface* bli);
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
    zx_status_t ModifyCmd(uint16_t clr_bits, uint16_t set_bits) TA_EXCL(dev_lock_);

    // Enable or disable bus mastering in a device's configuration.
    //
    // @param enable If true, allow the device to access main system memory as a bus
    // master.
    // @return A zx_status_t indicating success or failure of the operation.
    zx_status_t EnableBusMaster(bool enabled) TA_EXCL(dev_lock_);

    // Enable or disable PIO access in a device's configuration.
    //
    // @param enable If true, allow the device to access its PIO mapped registers.
    // @return A zx_status_t indicating success or failure of the operation.
    zx_status_t EnablePio(bool enabled) TA_EXCL(dev_lock_);

    // Enable or disable MMIO access in a device's configuration.
    //
    // @param enable If true, allow the device to access its MMIO mapped registers.
    // @return A zx_status_t indicating success or failure of the operation.
    zx_status_t EnableMmio(bool enabled) TA_EXCL(dev_lock_);

    // Return information about the requested base address register, if it has been
    // allocated.
    //
    // @param bar_id The index of the BAR register to fetch info for
    // @param out_info A pointer to a BarInfo buffer to store the bar info
    //
    // @return ZX_OK on success, ZX_INVALID_ARGS if bad a bar bar index or
    // pointer is passed in, and ZX_BAD_STATE if the device is disabled.
    zx_status_t GetBarInfo(uint32_t bar_id, const BarInfo* out_info) const;

    // Requests a device unplug itself from its UpstreamNode and the Bus list.
    virtual void Unplug() TA_EXCL(dev_lock_);
    // TODO(cja): port void SetQuirksDone() TA_REQ(dev_lock_) { quirks_done_ = true; }
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
    uint8_t bus_id() const { return cfg_->bdf().bus_id; }
    uint8_t dev_id() const { return cfg_->bdf().device_id; }
    uint8_t func_id() const { return cfg_->bdf().function_id; }
    uint32_t bar_count() const { return bar_count_; }
    const CapabilityList& capabilities() const { return caps_.list; }

    // Dump some information about the device
    virtual void Dump() const;

    // Devices need to exist in both the top level bus driver class, as well
    // as in a list for roots/bridges to track their downstream children. These
    // traits facilitate that for us.
protected:
    Device(zx_device_t* parent,
           fbl::RefPtr<Config>&& config,
           UpstreamNode* upstream,
           BusLinkInterface* bli,
           bool is_bridge)
        : PciDeviceType(parent),
          is_bridge_(is_bridge),
          cfg_(std::move(config)),
          bar_count_(is_bridge ? PCI_BAR_REGS_PER_BRIDGE : PCI_BAR_REGS_PER_DEVICE),
          upstream_(upstream),
          bli_(bli) {}

    zx_status_t Init() TA_EXCL(dev_lock_);
    zx_status_t InitLocked() TA_REQ(dev_lock_);
    fbl::Mutex* dev_lock() { return &dev_lock_; }

    // Read the value of the Command register, requires the dev_lock.
    uint16_t ReadCmdLocked() TA_REQ(dev_lock_) TA_EXCL(cmd_reg_lock_) {
        fbl::AutoLock cmd_lock(&cmd_reg_lock_);
        return cfg_->Read(Config::kCommand);
    }
    void ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits)
        TA_REQ(dev_lock_) TA_EXCL(cmd_reg_lock_);
    void AssignCmdLocked(uint16_t value) TA_REQ(dev_lock_) TA_EXCL(cmd_reg_lock_) {
        ModifyCmdLocked(0xFFFF, value);
    }

    bool IoEnabled() TA_REQ(dev_lock_) {
        return ReadCmdLocked() & PCI_COMMAND_IO_EN;
    }

    bool MmioEnabled() TA_REQ(dev_lock_) {
        return ReadCmdLocked() & PCI_COMMAND_MEM_EN;
    }

    zx_status_t ProbeCapabilities() TA_REQ(dev_lock_);
    zx_status_t ParseCapabilities() TA_REQ(dev_lock_);
    // TODO(cja) port zx_status_t ParseExtendedCapabilities() TA_REQ(dev_lock_);

    fbl::Mutex cmd_reg_lock_;       // Protection for access to the command register.
    const bool is_bridge_;          // True if this device is also a bridge
    const fbl::RefPtr<Config> cfg_; // Pointer to the device's config interface.
    uint16_t vendor_id_;            // The device's vendor ID, as read from config
    uint16_t device_id_;            // The device's device ID, as read from config
    uint8_t class_id_;              // The device's class ID, as read from config.
    uint8_t subclass_;              // The device's subclass, as read from config.
    uint8_t prog_if_;               // The device's programming interface (from cfg)
    uint8_t rev_id_;                // The device's revision ID (from cfg)

    // State related to lifetime management.
    bool plugged_in_ = false;
    bool disabled_ = false;
    bool quirks_done_ = false;
    mutable fbl::Mutex dev_lock_;

    // Info about the BARs computed and cached during the initial setup/probe,
    // indexed by starting BAR register index.
    BarInfo bars_[PCI_MAX_BAR_REGS];
    const uint32_t bar_count_;

    // An upstream node will outlive its downstream devices
    UpstreamNode* upstream_; // The upstream node in the device graph.
    BusLinkInterface* const bli_;

private:
    // Allow UpstreamNode implementations to Probe/Allocate/Configure/Disable.
    friend class UpstreamNode;
    friend class Bridge;
    friend class Root;
    // Probes a BAR's configuration. If it is already allocated it will try to
    // reserve the existing address window for it so that devices configured by system
    // firmware can be maintained as much as possible.
    zx_status_t ProbeBar(uint32_t bar_id) TA_REQ(dev_lock_);
    // Allocates address space for a BAR if it does not already exist.
    zx_status_t AllocateBar(uint32_t bar_id) TA_REQ(dev_lock_);
    // Called a device to configure (probe/allocate) its BARs
    zx_status_t ConfigureBarsLocked() TA_REQ(dev_lock_);
    // Called by an UpstreamNode to configure the BARs of a device downsteream.
    // Bridge implements it so it can allocate its bridge windows and own BARs before
    // configuring downstream BARs..
    virtual zx_status_t ConfigureBars() TA_EXCL(dev_lock_);

    // Disable a device, and anything downstream of it.  The device will
    // continue to enumerate, but users will only be able to access config (and
    // only in a read only fashion).  BAR windows, bus mastering, and interrupts
    // will all be disabled.
    virtual void Disable() TA_EXCL(dev_lock_);
    void DisableLocked() TA_REQ(dev_lock_);

    Capabilities caps_ = {};

    // IRQ structures
    // TODO(cja): Port over the IRQ support from kernel pci.

    // These allow a device to exist in an upstream node list as well
    // as the top level bus list of all devices.
    friend struct BusListTraits;
    friend struct UpstreamListTraits;
    NodeState bus_list_state_;
};

} // namespace pci

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_DEVICE_H_
