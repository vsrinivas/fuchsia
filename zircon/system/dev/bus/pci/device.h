// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_DEVICE_H_

#include <assert.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>

#include <ddktl/device.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <hw/pci.h>
#include <region-alloc/region-alloc.h>

#include "allocation.h"
#include "bar_info.h"
#include "capabilities.h"
#include "capabilities/msi.h"
#include "capabilities/msix.h"
#include "capabilities/pci_express.h"
#include "config.h"
#include "device_proxy.h"
#include "ref_counted.h"

namespace pci {

// UpstreamNode includes device.h, so only forward declare it here.
class UpstreamNode;
class BusLinkInterface;

// A pci::Device represents a given PCI(e) device on a bus. It can be used
// standalone for a regular PCI(e) device on the bus, or as the base class for a
// Bridge. Most work a pci::Device does is limited to its own registers in
// configuration space and are managed through their Config object handled to it
// during creation. One of the biggest responsibilities of the pci::Device class
// is fulfill the PCI protocol for the driver downstream operating the PCI
// device this corresponds to.
class Device;
using PciDeviceType = ddk::Device<pci::Device, ddk::Rxrpcable>;
class Device : public PciDeviceType, public fbl::DoublyLinkedListable<Device*> {
 public:
  using NodeState = fbl::WAVLTreeNodeState<fbl::RefPtr<pci::Device>>;
  struct BusListTraits {
    static NodeState& node_state(Device& device) { return device.bus_list_state_; }
  };

  // These traits are used for the WAVL tree implementation. They allow device objects
  // to be sorted and found in trees by composite bdf address.
  struct KeyTraitsSortByBdf {
    static const pci_bdf_t& GetKey(pci::Device& dev) { return dev.cfg_->bdf(); }

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
    ExtCapabilityList ext_list;
    MsiCapability* msi;
    MsixCapability* msix;
    PciExpressCapability* pcie;
  };

  // DDKTL PciProtocol methods that will be called by Rxrpc.
  zx_status_t RpcConfigRead(const zx::unowned_channel& ch);
  zx_status_t RpcConfigWrite(const zx::unowned_channel& ch);
  zx_status_t RpcEnableBusMaster(const zx::unowned_channel& ch);
  zx_status_t RpcGetAuxdata(const zx::unowned_channel& ch);
  zx_status_t RpcGetBar(const zx::unowned_channel& ch);
  zx_status_t RpcGetBti(const zx::unowned_channel& ch);
  zx_status_t RpcGetDeviceInfo(const zx::unowned_channel& ch);
  zx_status_t RpcGetNextCapability(const zx::unowned_channel& ch);
  zx_status_t RpcMapInterrupt(const zx::unowned_channel& ch);
  zx_status_t RpcQueryIrqMode(const zx::unowned_channel& ch);
  zx_status_t RpcResetDevice(const zx::unowned_channel& ch);
  zx_status_t RpcSetIrqMode(const zx::unowned_channel& ch);
  zx_status_t DdkRxrpc(zx_handle_t channel);

  // DDK mix-in impls
  void DdkRelease() { delete this; }

  // Create, but do not initialize, a device.
  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Config>&& config,
                            UpstreamNode* upstream, BusLinkInterface* bli);
  zx_status_t CreateProxy();
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

  // Requests a device unplug itself from its UpstreamNode and the Bus list.
  virtual void Unplug() TA_EXCL(dev_lock_);
  // TODO(cja): port void SetQuirksDone() TA_REQ(dev_lock_) { quirks_done_ = true; }
  const std::unique_ptr<Config>& config() const { return cfg_; }

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
  const Capabilities& capabilities() const { return caps_; }
  const BarInfo& GetBar(uint8_t bar_id) {
    ZX_DEBUG_ASSERT(bar_id < bar_count_);
    return bars_[bar_id];
  }
  // Dump some information about the device
  virtual void Dump() const;

  // Devices need to exist in both the top level bus driver class, as well
  // as in a list for roots/bridges to track their downstream children. These
  // traits facilitate that for us.
 protected:
  Device(zx_device_t* parent, std::unique_ptr<Config>&& config, UpstreamNode* upstream,
         BusLinkInterface* bli, bool is_bridge)
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
  void ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits) TA_REQ(dev_lock_)
      TA_EXCL(cmd_reg_lock_);
  void AssignCmdLocked(uint16_t value) TA_REQ(dev_lock_) TA_EXCL(cmd_reg_lock_) {
    ModifyCmdLocked(0xFFFF, value);
  }

  bool IoEnabled() TA_REQ(dev_lock_) { return ReadCmdLocked() & PCI_COMMAND_IO_EN; }

  bool MmioEnabled() TA_REQ(dev_lock_) { return ReadCmdLocked() & PCI_COMMAND_MEM_EN; }

  zx_status_t ProbeCapabilities() TA_REQ(dev_lock_);
  zx_status_t ParseCapabilities() TA_REQ(dev_lock_);
  zx_status_t ParseExtendedCapabilities() TA_REQ(dev_lock_);
  // TODO(cja) port zx_status_t ParseExtendedCapabilities() TA_REQ(dev_lock_);

  fbl::Mutex cmd_reg_lock_;            // Protection for access to the command register.
  const bool is_bridge_;               // True if this device is also a bridge
  const std::unique_ptr<Config> cfg_;  // Pointer to the device's config interface.
  uint16_t vendor_id_;                 // The device's vendor ID, as read from config
  uint16_t device_id_;                 // The device's device ID, as read from config
  uint8_t class_id_;                   // The device's class ID, as read from config.
  uint8_t subclass_;                   // The device's subclass, as read from config.
  uint8_t prog_if_;                    // The device's programming interface (from cfg)
  uint8_t rev_id_;                     // The device's revision ID (from cfg)

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
  UpstreamNode* upstream_;  // The upstream node in the device graph.
  BusLinkInterface* const bli_;

 private:
  zx_status_t RpcReply(const zx::unowned_channel& ch, zx_status_t st,
                       zx_handle_t* handles = nullptr, const uint32_t handle_cnt = 0);
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
  // Called by an UpstreamNode to configure the BARs of a device downstream.
  // Bridge implements it so it can allocate its bridge windows and own BARs before
  // configuring downstream BARs..
  virtual zx_status_t ConfigureBars() TA_EXCL(dev_lock_);
  zx_status_t ConfigureCapabilities() TA_EXCL(dev_lock_);

  // Disable a device, and anything downstream of it.  The device will
  // continue to enumerate, but users will only be able to access config (and
  // only in a read only fashion).  BAR windows, bus mastering, and interrupts
  // will all be disabled.
  virtual void Disable() TA_EXCL(dev_lock_);
  void DisableLocked() TA_REQ(dev_lock_);

  Capabilities caps_ = {};

  // IRQ structures
  // TODO(cja): Port over the IRQ support from kernel pci.

  // Used for Rxrpc / RpcReply for protocol buffers.
  PciRpcMsg request_;
  PciRpcMsg response_;

  // These allow a device to exist in an upstream node list as well
  // as the top level bus list of all devices.
  friend struct BusListTraits;
  friend struct UpstreamListTraits;
  NodeState bus_list_state_;
};

}  // namespace pci

#endif  // ZIRCON_SYSTEM_DEV_BUS_PCI_DEVICE_H_
