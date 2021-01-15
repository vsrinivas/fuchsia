// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_DEVICE_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_DEVICE_H_

#include <assert.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>

#include <limits>

#include <ddktl/device.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <hw/pci.h>
#include <region-alloc/region-alloc.h>

#include "allocation.h"
#include "bar_info.h"
#include "bus_device_interface.h"
#include "capabilities.h"
#include "capabilities/msi.h"
#include "capabilities/msix.h"
#include "capabilities/pci_express.h"
#include "config.h"
#include "device_rpc.h"
#include "ref_counted.h"

namespace pci {

// UpstreamNode includes device.h, so only forward declare it here.
class UpstreamNode;
class BusDeviceInterface;

struct DownstreamListTag {};
struct SharedIrqListTag {};

// A pci::Device represents a given PCI(e) device on a bus. It can be used
// standalone for a regular PCI(e) device on the bus, or as the base class for a
// Bridge. Most work a pci::Device does is limited to its own registers in
// configuration space and are managed through their Config object handled to it
// during creation. One of the biggest responsibilities of the pci::Device class
// is fulfill the PCI protocol for the driver downstream operating the PCI
// device this corresponds to.
class Device;
using PciDeviceType = ddk::Device<pci::Device, ddk::Rxrpcable>;
class Device : public PciDeviceType,
               public fbl::WAVLTreeContainable<fbl::RefPtr<pci::Device>>,
               public fbl::ContainableBaseClasses<
                   fbl::TaggedDoublyLinkedListable<Device*, DownstreamListTag>,
                   fbl::TaggedDoublyLinkedListable<Device*, SharedIrqListTag>>

{
 public:
  // These traits are used for the WAVL tree implementation. They allow device objects
  // to be sorted and found in trees by composite bdf address in the Bus.
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

  // This structure contains all bookkeeping and state for a device's
  // configured IRQ mode. It is initialized to PCI_IRQ_MODE_DISABLED.
  struct Irqs {
    pci_irq_mode_t mode;     // The mode currently configured.
    zx::msi msi_allocation;  // The MSI allocation object for MSI & MSI-X
  };

  struct Capabilities {
    CapabilityList list;
    ExtCapabilityList ext_list;
    MsiCapability* msi;
    MsixCapability* msix;
    PciExpressCapability* pcie;
  };

  // DDKTL PciProtocol methods that will be called by Rxrpc.
  zx_status_t RpcConfigureIrqMode(const zx::unowned_channel& ch);
  zx_status_t RpcConfigRead(const zx::unowned_channel& ch);
  zx_status_t RpcConfigWrite(const zx::unowned_channel& ch);
  zx_status_t RpcConnectSysmem(const zx::unowned_channel& ch, zx_handle_t channel);
  zx_status_t RpcEnableBusMaster(const zx::unowned_channel& ch);
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
                            UpstreamNode* upstream, BusDeviceInterface* bdi);
  zx_status_t CreateProxy();
  virtual ~Device();

  // Bridge or DeviceImpl will need to implement refcounting
  PCI_REQUIRE_REFCOUNTED;

  // Disallow copying, assigning and moving.
  DISALLOW_COPY_ASSIGN_AND_MOVE(Device);

  // Trigger a function level reset (if possible)
  // TODO(cja): port zx_status_t DoFunctionLevelReset() when we have a way to test it

  // The methods here are locking versions that are used primarily by the PCI
  // protocol implementation in device_protocol.cc.

  // Modify bits in the device's command register (in the device config space),
  // clearing the bits specified by clr_bits and setting the bits specified by set
  // bits.  Specifically, the operation will be applied as...
  //
  // WR(cmd, (RD(cmd) & ~clr) | set)
  //
  // @param clr_bits The mask of bits to be cleared.
  // @param clr_bits The mask of bits to be set.
  // @return A zx_status_t indicating success or failure of the operation.
  zx_status_t ModifyCmd(uint16_t clr_bits, uint16_t set_bits) __TA_EXCLUDES(dev_lock_);

  // Enable or disable bus mastering in a device's configuration.
  //
  // @param enable If true, allow the device to access main system memory as a bus
  // master.
  // @return A zx_status_t indicating success or failure of the operation.
  zx_status_t EnableBusMaster(bool enabled) __TA_EXCLUDES(dev_lock_);

  // Enable or disable PIO access in a device's configuration.
  //
  // @param enable If true, allow the device to access its PIO mapped registers.
  // @return A zx_status_t indicating success or failure of the operation.
  zx_status_t EnablePio(bool enabled) __TA_EXCLUDES(dev_lock_);

  // Enable or disable MMIO access in a device's configuration.
  //
  // @param enable If true, allow the device to access its MMIO mapped registers.
  // @return A zx_status_t indicating success or failure of the operation.
  zx_status_t EnableMmio(bool enabled) __TA_EXCLUDES(dev_lock_);

  // Requests a device unplug itself from its UpstreamNode and the Bus list.
  virtual void Unplug() __TA_EXCLUDES(dev_lock_);
  // TODO(cja): port void SetQuirksDone() __TA_REQUIRES(dev_lock_) { quirks_done_ = true; }
  const std::unique_ptr<Config>& config() const { return cfg_; }

  fbl::Mutex* dev_lock() __TA_RETURN_CAPABILITY(dev_lock_) { return &dev_lock_; }
  UpstreamNode* upstream() { return upstream_; }

  bool plugged_in() const __TA_REQUIRES(dev_lock_) { return plugged_in_; }
  bool disabled() const __TA_REQUIRES(dev_lock_) { return disabled_; }
  bool quirks_done() const __TA_REQUIRES(dev_lock_) { return quirks_done_; }
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
  BarInfo GetBar(uint8_t bar_id) const __TA_EXCLUDES(dev_lock_) {
    ZX_DEBUG_ASSERT(bar_id < bar_count_);
    fbl::AutoLock dev_lock(&dev_lock_);

    auto& bar = bars_[bar_id];
    BarInfo out_bar = {.size = bar.size,
                       .address = bar.address,
                       .bar_id = bar.bar_id,
                       .is_mmio = bar.is_mmio,
                       .is_64bit = bar.is_64bit,
                       .is_prefetchable = bar.is_prefetchable};
    return out_bar;
  }

  // A packed version of the BDF addr used for BTI identifiers by the IOMMU implementation.
  uint32_t packed_addr() const {
    auto bdf = cfg_->bdf();
    return static_cast<uint32_t>((bdf.bus_id << 8) | (bdf.device_id << 3) | bdf.function_id);
  }

  // These methods handle IRQ configuration and are generally called by the
  // PciProtocol methods, though they may be used to disable IRQs on
  // initialization as well.
  zx::status<uint32_t> QueryIrqMode(pci_irq_mode_t mode) __TA_EXCLUDES(dev_lock_);
  zx_status_t SetIrqMode(pci_irq_mode_t mode, uint32_t irq_cnt) __TA_EXCLUDES(dev_lock_);
  zx::status<zx::interrupt> MapInterrupt(uint32_t which_irq) __TA_EXCLUDES(dev_lock_);
  zx_status_t DisableInterrupts() __TA_REQUIRES(dev_lock_);
  zx_status_t EnableMsi(uint32_t irq_cnt) __TA_REQUIRES(dev_lock_);
  zx_status_t EnableMsix(uint32_t irq_cnt) __TA_REQUIRES(dev_lock_);
  zx_status_t DisableMsi() __TA_REQUIRES(dev_lock_);
  zx_status_t DisableMsix() __TA_REQUIRES(dev_lock_);

  // Devices need to exist in both the top level bus driver class, as well
  // as in a list for roots/bridges to track their downstream children. These
  // traits facilitate that for us.
 protected:
  Device(zx_device_t* parent, std::unique_ptr<Config>&& config, UpstreamNode* upstream,
         BusDeviceInterface* bdi, bool is_bridge)
      : PciDeviceType(parent),
        cfg_(std::move(config)),
        upstream_(upstream),
        bdi_(bdi),
        bar_count_(is_bridge ? PCI_BAR_REGS_PER_BRIDGE : PCI_BAR_REGS_PER_DEVICE),
        is_bridge_(is_bridge) {}

  zx_status_t Init() __TA_EXCLUDES(dev_lock_);
  zx_status_t InitLocked() __TA_REQUIRES(dev_lock_);
  zx_status_t InitInterrupts() __TA_REQUIRES(dev_lock_);

  // Read the value of the Command register, requires the dev_lock.
  uint16_t ReadCmdLocked() __TA_REQUIRES(dev_lock_) __TA_EXCLUDES(cmd_reg_lock_) {
    fbl::AutoLock cmd_lock(&cmd_reg_lock_);
    return cfg_->Read(Config::kCommand);
  }
  void ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits) __TA_REQUIRES(dev_lock_)
      __TA_EXCLUDES(cmd_reg_lock_);
  void AssignCmdLocked(uint16_t value) __TA_REQUIRES(dev_lock_) __TA_EXCLUDES(cmd_reg_lock_) {
    ModifyCmdLocked(UINT16_MAX, value);
  }

  bool IoEnabled() __TA_REQUIRES(dev_lock_) { return ReadCmdLocked() & PCI_CFG_COMMAND_IO_EN; }
  bool MmioEnabled() __TA_REQUIRES(dev_lock_) { return ReadCmdLocked() & PCI_CFG_COMMAND_MEM_EN; }

  zx_status_t ProbeCapabilities() __TA_REQUIRES(dev_lock_);
  zx_status_t ParseCapabilities() __TA_REQUIRES(dev_lock_);
  zx_status_t ParseExtendedCapabilities() __TA_REQUIRES(dev_lock_);

  // Info about the BARs computed and cached during the initial setup/probe,
  // indexed by starting BAR register index.
  std::array<Bar, PCI_MAX_BAR_REGS>& bars() __TA_REQUIRES(dev_lock_) { return bars_; }
  BusDeviceInterface* bdi() __TA_REQUIRES(dev_lock_) { return bdi_; }

 private:
  zx_status_t RpcReply(const zx::unowned_channel& ch, zx_status_t st,
                       zx_handle_t* handles = nullptr, uint32_t handle_cnt = 0);
  // Allow UpstreamNode implementations to Probe/Allocate/Configure/Disable.
  friend class UpstreamNode;
  friend class Bridge;
  friend class Root;
  // Probes a BAR's configuration. If it is already allocated it will try to
  // reserve the existing address window for it so that devices configured by system
  // firmware can be maintained as much as possible.
  zx_status_t ProbeBar(uint8_t bar_id) __TA_REQUIRES(dev_lock_);
  // Allocates address space for a BAR out of any suitable allocators.
  zx::status<std::unique_ptr<PciAllocation>> AllocateFromUpstream(const Bar& bar,
                                                                  std::optional<zx_paddr_t> base)
      __TA_REQUIRES(dev_lock_);
  zx_status_t WriteBarInformation(const Bar& bar) __TA_REQUIRES(dev_lock_);

  // Allocates address space for a BAR if it does not already exist.
  zx_status_t AllocateBar(uint8_t bar_id) __TA_REQUIRES(dev_lock_);
  // Called a device to configure (probe/allocate) its BARs
  zx_status_t ConfigureBarsLocked() __TA_REQUIRES(dev_lock_);
  // Called by an UpstreamNode to configure the BARs of a device downstream.
  // Bridge implements it so it can allocate its bridge windows and own BARs before
  // configuring downstream BARs..
  virtual zx_status_t ConfigureBars() __TA_EXCLUDES(dev_lock_);
  zx_status_t ConfigureCapabilities() __TA_EXCLUDES(dev_lock_);
  zx::status<std::pair<zx::msi, zx_info_msi_t>> AllocateMsi(uint32_t irq_cnt)
      __TA_REQUIRES(dev_lock_);
  zx_status_t VerifyAllMsisFreed() __TA_REQUIRES(dev_lock_);

  // Disable a device, and anything downstream of it.  The device will
  // continue to enumerate, but users will only be able to access config (and
  // only in a read only fashion).  BAR windows, bus mastering, and interrupts
  // will all be disabled.
  virtual void Disable() __TA_EXCLUDES(dev_lock_);
  void DisableLocked() __TA_REQUIRES(dev_lock_);

  mutable fbl::Mutex dev_lock_;
  mutable fbl::Mutex cmd_reg_lock_;    // Protection for access to the command register.
  const std::unique_ptr<Config> cfg_;  // Pointer to the device's config interface.
  UpstreamNode* upstream_;             // The upstream node in the device graph.
  BusDeviceInterface* bdi_ __TA_GUARDED(dev_lock_);
  std::array<Bar, PCI_MAX_BAR_REGS> bars_ __TA_GUARDED(dev_lock_) = {};
  const uint32_t bar_count_;

  const bool is_bridge_;  // True if this device is also a bridge
  uint16_t vendor_id_;    // The device's vendor ID, as read from config
  uint16_t device_id_;    // The device's device ID, as read from config
  uint8_t class_id_;      // The device's class ID, as read from config.
  uint8_t subclass_;      // The device's subclass, as read from config.
  uint8_t prog_if_;       // The device's programming interface (from cfg)
  uint8_t rev_id_;        // The device's revision ID (from cfg)

  // State related to lifetime management.
  bool plugged_in_ __TA_GUARDED(dev_lock_) = false;
  bool disabled_ __TA_GUARDED(dev_lock_) = false;
  bool quirks_done_ __TA_GUARDED(dev_lock_) = false;

  Capabilities caps_ __TA_GUARDED(dev_lock_){};
  Irqs irqs_ __TA_GUARDED(dev_lock_){.mode = PCI_IRQ_MODE_DISABLED};

  // Used for Rxrpc / RpcReply for protocol buffers.
  PciRpcMsg request_;
  PciRpcMsg response_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_DEVICE_H_
