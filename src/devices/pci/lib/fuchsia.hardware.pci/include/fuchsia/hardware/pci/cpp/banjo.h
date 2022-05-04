// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.pci banjo file

#ifndef SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_H_
#define SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_H_

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddktl/device-internal.h>

#include "banjo-internal.h"

// DDK pci-protocol support
//
// :: Proxies ::
//
// ddk::PciProtocolClient is a simple wrapper around
// pci_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::PciProtocol is a mixin class that simplifies writing DDK drivers
// that implement the pci protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PCI device.
// class PciDevice;
// using PciDeviceType = ddk::Device<PciDevice, /* ddk mixins */>;
//
// class PciDevice : public PciDeviceType,
//                      public ddk::PciProtocol<PciDevice> {
//   public:
//     PciDevice(zx_device_t* parent)
//         : PciDeviceType(parent) {}
//
//     zx_status_t PciGetDeviceInfo(pci_device_info_t* out_info);
//
//     zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_result);
//
//     zx_status_t PciSetBusMastering(bool enabled);
//
//     zx_status_t PciResetDevice();
//
//     zx_status_t PciAckInterrupt();
//
//     zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt);
//
//     void PciGetInterruptModes(pci_interrupt_modes_t* out_modes);
//
//     zx_status_t PciSetInterruptMode(pci_interrupt_mode_t mode, uint32_t requested_irq_count);
//
//     zx_status_t PciReadConfig8(uint16_t offset, uint8_t* out_value);
//
//     zx_status_t PciReadConfig16(uint16_t offset, uint16_t* out_value);
//
//     zx_status_t PciReadConfig32(uint16_t offset, uint32_t* out_value);
//
//     zx_status_t PciWriteConfig8(uint16_t offset, uint8_t value);
//
//     zx_status_t PciWriteConfig16(uint16_t offset, uint16_t value);
//
//     zx_status_t PciWriteConfig32(uint16_t offset, uint32_t value);
//
//     zx_status_t PciGetFirstCapability(pci_capability_id_t id, uint8_t* out_offset);
//
//     zx_status_t PciGetNextCapability(pci_capability_id_t id, uint8_t start_offset, uint8_t*
//     out_offset);
//
//     zx_status_t PciGetFirstExtendedCapability(pci_extended_capability_id_t id, uint16_t*
//     out_offset);
//
//     zx_status_t PciGetNextExtendedCapability(pci_extended_capability_id_t id, uint16_t
//     start_offset, uint16_t* out_offset);
//
//     zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class PciProtocol : public Base {
 public:
  PciProtocol() {
    internal::CheckPciProtocolSubclass<D>();
    pci_protocol_ops_.get_device_info = PciGetDeviceInfo;
    pci_protocol_ops_.get_bar = PciGetBar;
    pci_protocol_ops_.set_bus_mastering = PciSetBusMastering;
    pci_protocol_ops_.reset_device = PciResetDevice;
    pci_protocol_ops_.ack_interrupt = PciAckInterrupt;
    pci_protocol_ops_.map_interrupt = PciMapInterrupt;
    pci_protocol_ops_.get_interrupt_modes = PciGetInterruptModes;
    pci_protocol_ops_.set_interrupt_mode = PciSetInterruptMode;
    pci_protocol_ops_.read_config8 = PciReadConfig8;
    pci_protocol_ops_.read_config16 = PciReadConfig16;
    pci_protocol_ops_.read_config32 = PciReadConfig32;
    pci_protocol_ops_.write_config8 = PciWriteConfig8;
    pci_protocol_ops_.write_config16 = PciWriteConfig16;
    pci_protocol_ops_.write_config32 = PciWriteConfig32;
    pci_protocol_ops_.get_first_capability = PciGetFirstCapability;
    pci_protocol_ops_.get_next_capability = PciGetNextCapability;
    pci_protocol_ops_.get_first_extended_capability = PciGetFirstExtendedCapability;
    pci_protocol_ops_.get_next_extended_capability = PciGetNextExtendedCapability;
    pci_protocol_ops_.get_bti = PciGetBti;

    if constexpr (internal::is_base_proto<Base>::value) {
      auto dev = static_cast<D*>(this);
      // Can only inherit from one base_protocol implementation.
      ZX_ASSERT(dev->ddk_proto_id_ == 0);
      dev->ddk_proto_id_ = ZX_PROTOCOL_PCI;
      dev->ddk_proto_ops_ = &pci_protocol_ops_;
    }
  }

 protected:
  pci_protocol_ops_t pci_protocol_ops_ = {};

 private:
  // Returns a structure containing device information from the configuration header.
  static zx_status_t PciGetDeviceInfo(void* ctx, pci_device_info_t* out_info) {
    auto ret = static_cast<D*>(ctx)->PciGetDeviceInfo(out_info);
    return ret;
  }
  // Retrieves information for a specified Base Address Register.
  //
  // Parameters:
  // |bar_id|: The id of the BAR being requested. Valid range is [0, 6).
  //
  // Errors:
  // |ZX_ERR_INTERNAL|: A bus driver error has occurred.
  // |ZX_ERR_INVALID_ARGS|: The |bar_id| specified is outside of the acceptable range.
  // |ZX_ERR_NOT_FOUND|: The specified |bar_id| does not exist for this device.
  static zx_status_t PciGetBar(void* ctx, uint32_t bar_id, pci_bar_t* out_result) {
    auto ret = static_cast<D*>(ctx)->PciGetBar(bar_id, out_result);
    return ret;
  }
  // Enables or disables the bus mastering capability for the device.
  //
  // Parameters:
  // |enable|: true to enable bus mastering, false to disable.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: Method was called while the device is disabled.
  static zx_status_t PciSetBusMastering(void* ctx, bool enabled) {
    auto ret = static_cast<D*>(ctx)->PciSetBusMastering(enabled);
    return ret;
  }
  // Initiates a function level reset for the device. This is a synchronous
  // operation that will not return ontil the reset is complete. Interrupt
  // operation of the device must be disabled before initiating a reset.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: Interrupts were not disabled before calling |ResetDevice|.
  // |ZX_ERR_NOT_SUPPORTED|: The device does not support reset.
  // |ZX_ERR_TIMED_OUT|: The device did not complete its reset in the
  // expected amount of time and is presumed to no longer be operating
  // properly.
  static zx_status_t PciResetDevice(void* ctx) {
    auto ret = static_cast<D*>(ctx)->PciResetDevice();
    return ret;
  }
  // Alerts the bus driver to deassert the raised legacy interrupt so that it
  // may be waited on again. Only used if |SetInterruptMode| was called with
  // |PCI_INTERRUPT_MODE_LEGACY|.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: device is not configured to use the Legacy interrupt mode.
  static zx_status_t PciAckInterrupt(void* ctx) {
    auto ret = static_cast<D*>(ctx)->PciAckInterrupt();
    return ret;
  }
  // Maps a device's interrupt to a zx:interrupt. The device's interrupt mode
  // must already be configured with |SetInterruptMode|, and |which_irq| must
  // be >= to the number of interrupts reported for that interrupt mode by
  // |GetInterruptModes|. A Legacy interrupt may be mapped multiple times,
  // but the handles will point to the same interrupt object. MSI & MSI-X
  // interrupts may only have one outstanding mapping at a time per
  // interrupt. Outstanding MSI & MSI-X interrupt handles must be closed
  // before attempting to change the interrupt mode in a subsequent call to
  // |SetInterruptMode|.
  //
  // Parameters:
  // |which_irq|: The id of the interrupt to map.
  //
  // Errors:
  // |ZX_ERR_ALREADY_BOUND|: The interrupt specified by |which_irq| is
  // already mapped to a valid handle.
  // |ZX_ERR_BAD_STATE|: interrupts are currently disabled for the device.
  // |ZX_ERR_INVALID_ARGS|: |which_irq| is invalid for the mode.
  static zx_status_t PciMapInterrupt(void* ctx, uint32_t which_irq, zx_handle_t* out_interrupt) {
    zx::interrupt out_interrupt2;
    auto ret = static_cast<D*>(ctx)->PciMapInterrupt(which_irq, &out_interrupt2);
    *out_interrupt = out_interrupt2.release();
    return ret;
  }
  // Returns the supported interrupt modes for a device.
  static void PciGetInterruptModes(void* ctx, pci_interrupt_modes_t* out_modes) {
    static_cast<D*>(ctx)->PciGetInterruptModes(out_modes);
  }
  // Configures the interrupt mode for a device. When changing from one
  // interrupt mode to another the driver must ensure existing interrupt
  // handles are closed beforehand.
  //
  // Parameters:
  // |mode|: The |PciInterruptMode| to request from the bus driver.
  // |requested_irq_count|: The number of interrupts requested.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: The driver attempted to change interrupt mode while
  // existing handles to mapped MSIs exist.
  // |ZX_ERR_INVALID_ARGS|: |requested_irq_count| is 0.
  // |ZX_ERR_NOT_SUPPORTED|: The provided |mode| is not supported, or invalid.
  static zx_status_t PciSetInterruptMode(void* ctx, pci_interrupt_mode_t mode,
                                         uint32_t requested_irq_count) {
    auto ret = static_cast<D*>(ctx)->PciSetInterruptMode(mode, requested_irq_count);
    return ret;
  }
  // Reads a byte from the device's configuration space. |Offset| must be
  // within [0x0, 0xFF] if PCI, or [0x0, 0xFFF) if PCIe. In most cases a
  // device will be PCIe.
  //
  // Parameters:
  // |offset|: The offset into the device's configuration space to read.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  static zx_status_t PciReadConfig8(void* ctx, uint16_t offset, uint8_t* out_value) {
    auto ret = static_cast<D*>(ctx)->PciReadConfig8(offset, out_value);
    return ret;
  }
  // Reads two bytes from the device's configuration space. |Offset| must be
  // within [0x0, 0xFE] if PCI, or [0x0, 0xFFE] if PCIe. In most cases a
  // device will be PCIe.
  //
  // Parameters:
  // |offset|: The offset into the device's configuration space to read.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  static zx_status_t PciReadConfig16(void* ctx, uint16_t offset, uint16_t* out_value) {
    auto ret = static_cast<D*>(ctx)->PciReadConfig16(offset, out_value);
    return ret;
  }
  // Reads four bytes from the device's configuration space. |Offset| must be
  // within [0x0, 0xFC] if PCI, or [0x0, 0xFFC] if PCIe. In most cases a
  // device will be PCIe.
  //
  // Parameters:
  // |offset|: The offset into the device's configuration space to read.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  static zx_status_t PciReadConfig32(void* ctx, uint16_t offset, uint32_t* out_value) {
    auto ret = static_cast<D*>(ctx)->PciReadConfig32(offset, out_value);
    return ret;
  }
  // Writes a byte to the device's configuration space. The acceptable
  // ranges of |offset| for writes are [0x40, 0xFF] if PCI, or [0x40,
  // 0xFFF] if PCIe. For most purposes a device will be PCIe.
  //
  //
  // Parameters
  // |offset|: The offset into the device's configuration space to read.
  // |value|: The value to write.
  //
  // Errors:
  // |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  static zx_status_t PciWriteConfig8(void* ctx, uint16_t offset, uint8_t value) {
    auto ret = static_cast<D*>(ctx)->PciWriteConfig8(offset, value);
    return ret;
  }
  // Writes two bytes to the device's configuration space. The acceptable
  // ranges of |offset| for writes are [0x40, 0xFE] if PCI, or [0x40,
  // 0xFFE] if PCIe. For most purposes a device will be PCIe.
  //
  //
  // Parameters
  // |offset|: The offset into the device's configuration space to read.
  // |value|: The value to write.
  //
  // Errors:
  // |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  static zx_status_t PciWriteConfig16(void* ctx, uint16_t offset, uint16_t value) {
    auto ret = static_cast<D*>(ctx)->PciWriteConfig16(offset, value);
    return ret;
  }
  // Writes four bytes to the device's configuration space. The acceptable
  // ranges of |offset| for writes are [0x40, 0xFC] if PCI, or [0x40,
  // 0xFFC] if PCIe. For most purposes a device will be PCIe.
  //
  //
  // Parameters
  // |offset|: The offset into the device's configuration space to read.
  // |value|: The value to write.
  //
  // Errors:
  // |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  static zx_status_t PciWriteConfig32(void* ctx, uint16_t offset, uint32_t value) {
    auto ret = static_cast<D*>(ctx)->PciWriteConfig32(offset, value);
    return ret;
  }
  // Returns the offset into the device's configuration space of the first
  // capability matching the capability id.
  //
  // Parameters:
  // |id|: the capability id to search for.
  //
  // Errors:
  // |ZX_ERR_NOT_FOUND|: A capability of id |id| was not found.
  static zx_status_t PciGetFirstCapability(void* ctx, pci_capability_id_t id, uint8_t* out_offset) {
    auto ret = static_cast<D*>(ctx)->PciGetFirstCapability(id, out_offset);
    return ret;
  }
  // Returns the offset into the device's configuration space of the next
  // capability matching the provided capability id, starting at |offset|.
  //
  // Parameters:
  // |id|: the capability id to search for.
  // |start_offset|: the configuration space offset of the previous
  // capability to start searching from.
  //
  // Errors:
  // |ZX_ERR_NOT_FOUND|: A capability of id |id| was not found in a scan
  // starting from |offset|
  static zx_status_t PciGetNextCapability(void* ctx, pci_capability_id_t id, uint8_t start_offset,
                                          uint8_t* out_offset) {
    auto ret = static_cast<D*>(ctx)->PciGetNextCapability(id, start_offset, out_offset);
    return ret;
  }
  // Returns the offset into the device's configuration space of first
  // extended capability matching the provided extended capability id.
  //
  // Parameters:
  // |id|: the capability id to search for
  //
  // Errors:
  // |ZX_ERR_NOT_FOUND|: A extended capability of id |id| was not found.
  static zx_status_t PciGetFirstExtendedCapability(void* ctx, pci_extended_capability_id_t id,
                                                   uint16_t* out_offset) {
    auto ret = static_cast<D*>(ctx)->PciGetFirstExtendedCapability(id, out_offset);
    return ret;
  }
  // Returns the offset into the device's configuration space of the next
  // extended capability matching the provided extended capability id,
  // starting at |offset|.
  //
  // Parameters:
  // |id|: the capability id to search for.
  // |start_offset|: the configuration space offset of the previous extended
  // capability to start searching from.
  //
  // Errors
  // |ZX_ERR_NOT_FOUND|: A extended capability of id |id| was not found in a
  // scan starting from |offset|.
  static zx_status_t PciGetNextExtendedCapability(void* ctx, pci_extended_capability_id_t id,
                                                  uint16_t start_offset, uint16_t* out_offset) {
    auto ret = static_cast<D*>(ctx)->PciGetNextExtendedCapability(id, start_offset, out_offset);
    return ret;
  }
  // Returns the Bus Transaction Intiator (BTI) at a given index for the device.
  //
  // Parameters:
  // |index|: the BTI to request.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |index| was not 0.
  static zx_status_t PciGetBti(void* ctx, uint32_t index, zx_handle_t* out_bti) {
    zx::bti out_bti2;
    auto ret = static_cast<D*>(ctx)->PciGetBti(index, &out_bti2);
    *out_bti = out_bti2.release();
    return ret;
  }
};

class PciProtocolClient {
 public:
  PciProtocolClient() : ops_(nullptr), ctx_(nullptr) {}
  PciProtocolClient(const pci_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

  PciProtocolClient(zx_device_t* parent) {
    pci_protocol_t proto;
    if (device_get_protocol(parent, ZX_PROTOCOL_PCI, &proto) == ZX_OK) {
      ops_ = proto.ops;
      ctx_ = proto.ctx;
    } else {
      ops_ = nullptr;
      ctx_ = nullptr;
    }
  }

  PciProtocolClient(zx_device_t* parent, const char* fragment_name) {
    pci_protocol_t proto;
    if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_PCI, &proto) == ZX_OK) {
      ops_ = proto.ops;
      ctx_ = proto.ctx;
    } else {
      ops_ = nullptr;
      ctx_ = nullptr;
    }
  }

  // Create a PciProtocolClient from the given parent device + "fragment".
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, PciProtocolClient* result) {
    pci_protocol_t proto;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PCI, &proto);
    if (status != ZX_OK) {
      return status;
    }
    *result = PciProtocolClient(&proto);
    return ZX_OK;
  }

  // Create a PciProtocolClient from the given parent device.
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                      PciProtocolClient* result) {
    pci_protocol_t proto;
    zx_status_t status =
        device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_PCI, &proto);
    if (status != ZX_OK) {
      return status;
    }
    *result = PciProtocolClient(&proto);
    return ZX_OK;
  }

  void GetProto(pci_protocol_t* proto) const {
    proto->ctx = ctx_;
    proto->ops = ops_;
  }
  bool is_valid() const { return ops_ != nullptr; }
  void clear() {
    ctx_ = nullptr;
    ops_ = nullptr;
  }

  // Returns a structure containing device information from the configuration header.
  zx_status_t GetDeviceInfo(pci_device_info_t* out_info) const {
    return ops_->get_device_info(ctx_, out_info);
  }

  // Retrieves information for a specified Base Address Register.
  //
  // Parameters:
  // |bar_id|: The id of the BAR being requested. Valid range is [0, 6).
  //
  // Errors:
  // |ZX_ERR_INTERNAL|: A bus driver error has occurred.
  // |ZX_ERR_INVALID_ARGS|: The |bar_id| specified is outside of the acceptable range.
  // |ZX_ERR_NOT_FOUND|: The specified |bar_id| does not exist for this device.
  zx_status_t GetBar(uint32_t bar_id, pci_bar_t* out_result) const {
    return ops_->get_bar(ctx_, bar_id, out_result);
  }

  // Enables or disables the bus mastering capability for the device.
  //
  // Parameters:
  // |enable|: true to enable bus mastering, false to disable.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: Method was called while the device is disabled.
  zx_status_t SetBusMastering(bool enabled) const { return ops_->set_bus_mastering(ctx_, enabled); }

  // Initiates a function level reset for the device. This is a synchronous
  // operation that will not return ontil the reset is complete. Interrupt
  // operation of the device must be disabled before initiating a reset.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: Interrupts were not disabled before calling |ResetDevice|.
  // |ZX_ERR_NOT_SUPPORTED|: The device does not support reset.
  // |ZX_ERR_TIMED_OUT|: The device did not complete its reset in the
  // expected amount of time and is presumed to no longer be operating
  // properly.
  zx_status_t ResetDevice() const { return ops_->reset_device(ctx_); }

  // Alerts the bus driver to deassert the raised legacy interrupt so that it
  // may be waited on again. Only used if |SetInterruptMode| was called with
  // |PCI_INTERRUPT_MODE_LEGACY|.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: device is not configured to use the Legacy interrupt mode.
  zx_status_t AckInterrupt() const { return ops_->ack_interrupt(ctx_); }

  // Maps a device's interrupt to a zx:interrupt. The device's interrupt mode
  // must already be configured with |SetInterruptMode|, and |which_irq| must
  // be >= to the number of interrupts reported for that interrupt mode by
  // |GetInterruptModes|. A Legacy interrupt may be mapped multiple times,
  // but the handles will point to the same interrupt object. MSI & MSI-X
  // interrupts may only have one outstanding mapping at a time per
  // interrupt. Outstanding MSI & MSI-X interrupt handles must be closed
  // before attempting to change the interrupt mode in a subsequent call to
  // |SetInterruptMode|.
  //
  // Parameters:
  // |which_irq|: The id of the interrupt to map.
  //
  // Errors:
  // |ZX_ERR_ALREADY_BOUND|: The interrupt specified by |which_irq| is
  // already mapped to a valid handle.
  // |ZX_ERR_BAD_STATE|: interrupts are currently disabled for the device.
  // |ZX_ERR_INVALID_ARGS|: |which_irq| is invalid for the mode.
  zx_status_t MapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt) const {
    return ops_->map_interrupt(ctx_, which_irq, out_interrupt->reset_and_get_address());
  }

  // Returns the supported interrupt modes for a device.
  void GetInterruptModes(pci_interrupt_modes_t* out_modes) const {
    ops_->get_interrupt_modes(ctx_, out_modes);
  }

  // Configures the interrupt mode for a device. When changing from one
  // interrupt mode to another the driver must ensure existing interrupt
  // handles are closed beforehand.
  //
  // Parameters:
  // |mode|: The |PciInterruptMode| to request from the bus driver.
  // |requested_irq_count|: The number of interrupts requested.
  //
  // Errors:
  // |ZX_ERR_BAD_STATE|: The driver attempted to change interrupt mode while
  // existing handles to mapped MSIs exist.
  // |ZX_ERR_INVALID_ARGS|: |requested_irq_count| is 0.
  // |ZX_ERR_NOT_SUPPORTED|: The provided |mode| is not supported, or invalid.
  zx_status_t SetInterruptMode(pci_interrupt_mode_t mode, uint32_t requested_irq_count) const {
    return ops_->set_interrupt_mode(ctx_, mode, requested_irq_count);
  }

  // Reads a byte from the device's configuration space. |Offset| must be
  // within [0x0, 0xFF] if PCI, or [0x0, 0xFFF) if PCIe. In most cases a
  // device will be PCIe.
  //
  // Parameters:
  // |offset|: The offset into the device's configuration space to read.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  zx_status_t ReadConfig8(uint16_t offset, uint8_t* out_value) const {
    return ops_->read_config8(ctx_, offset, out_value);
  }

  // Reads two bytes from the device's configuration space. |Offset| must be
  // within [0x0, 0xFE] if PCI, or [0x0, 0xFFE] if PCIe. In most cases a
  // device will be PCIe.
  //
  // Parameters:
  // |offset|: The offset into the device's configuration space to read.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  zx_status_t ReadConfig16(uint16_t offset, uint16_t* out_value) const {
    return ops_->read_config16(ctx_, offset, out_value);
  }

  // Reads four bytes from the device's configuration space. |Offset| must be
  // within [0x0, 0xFC] if PCI, or [0x0, 0xFFC] if PCIe. In most cases a
  // device will be PCIe.
  //
  // Parameters:
  // |offset|: The offset into the device's configuration space to read.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  zx_status_t ReadConfig32(uint16_t offset, uint32_t* out_value) const {
    return ops_->read_config32(ctx_, offset, out_value);
  }

  // Writes a byte to the device's configuration space. The acceptable
  // ranges of |offset| for writes are [0x40, 0xFF] if PCI, or [0x40,
  // 0xFFF] if PCIe. For most purposes a device will be PCIe.
  //
  //
  // Parameters
  // |offset|: The offset into the device's configuration space to read.
  // |value|: The value to write.
  //
  // Errors:
  // |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  zx_status_t WriteConfig8(uint16_t offset, uint8_t value) const {
    return ops_->write_config8(ctx_, offset, value);
  }

  // Writes two bytes to the device's configuration space. The acceptable
  // ranges of |offset| for writes are [0x40, 0xFE] if PCI, or [0x40,
  // 0xFFE] if PCIe. For most purposes a device will be PCIe.
  //
  //
  // Parameters
  // |offset|: The offset into the device's configuration space to read.
  // |value|: The value to write.
  //
  // Errors:
  // |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  zx_status_t WriteConfig16(uint16_t offset, uint16_t value) const {
    return ops_->write_config16(ctx_, offset, value);
  }

  // Writes four bytes to the device's configuration space. The acceptable
  // ranges of |offset| for writes are [0x40, 0xFC] if PCI, or [0x40,
  // 0xFFC] if PCIe. For most purposes a device will be PCIe.
  //
  //
  // Parameters
  // |offset|: The offset into the device's configuration space to read.
  // |value|: The value to write.
  //
  // Errors:
  // |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
  // |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
  zx_status_t WriteConfig32(uint16_t offset, uint32_t value) const {
    return ops_->write_config32(ctx_, offset, value);
  }

  // Returns the offset into the device's configuration space of the first
  // capability matching the capability id.
  //
  // Parameters:
  // |id|: the capability id to search for.
  //
  // Errors:
  // |ZX_ERR_NOT_FOUND|: A capability of id |id| was not found.
  zx_status_t GetFirstCapability(pci_capability_id_t id, uint8_t* out_offset) const {
    return ops_->get_first_capability(ctx_, id, out_offset);
  }

  // Returns the offset into the device's configuration space of the next
  // capability matching the provided capability id, starting at |offset|.
  //
  // Parameters:
  // |id|: the capability id to search for.
  // |start_offset|: the configuration space offset of the previous
  // capability to start searching from.
  //
  // Errors:
  // |ZX_ERR_NOT_FOUND|: A capability of id |id| was not found in a scan
  // starting from |offset|
  zx_status_t GetNextCapability(pci_capability_id_t id, uint8_t start_offset,
                                uint8_t* out_offset) const {
    return ops_->get_next_capability(ctx_, id, start_offset, out_offset);
  }

  // Returns the offset into the device's configuration space of first
  // extended capability matching the provided extended capability id.
  //
  // Parameters:
  // |id|: the capability id to search for
  //
  // Errors:
  // |ZX_ERR_NOT_FOUND|: A extended capability of id |id| was not found.
  zx_status_t GetFirstExtendedCapability(pci_extended_capability_id_t id,
                                         uint16_t* out_offset) const {
    return ops_->get_first_extended_capability(ctx_, id, out_offset);
  }

  // Returns the offset into the device's configuration space of the next
  // extended capability matching the provided extended capability id,
  // starting at |offset|.
  //
  // Parameters:
  // |id|: the capability id to search for.
  // |start_offset|: the configuration space offset of the previous extended
  // capability to start searching from.
  //
  // Errors
  // |ZX_ERR_NOT_FOUND|: A extended capability of id |id| was not found in a
  // scan starting from |offset|.
  zx_status_t GetNextExtendedCapability(pci_extended_capability_id_t id, uint16_t start_offset,
                                        uint16_t* out_offset) const {
    return ops_->get_next_extended_capability(ctx_, id, start_offset, out_offset);
  }

  // Returns the Bus Transaction Intiator (BTI) at a given index for the device.
  //
  // Parameters:
  // |index|: the BTI to request.
  //
  // Errors:
  // |ZX_ERR_OUT_OF_RANGE|: |index| was not 0.
  zx_status_t GetBti(uint32_t index, zx::bti* out_bti) const {
    return ops_->get_bti(ctx_, index, out_bti->reset_and_get_address());
  }

 private:
  pci_protocol_ops_t* ops_;
  void* ctx_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_H_
