// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_DFU_USB_DFU_H_
#define SRC_DEVICES_USB_DRIVERS_USB_DFU_USB_DFU_H_

#include <fidl/fuchsia.hardware.usb.fwloader/cpp/wire.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/usb/dfu.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <usb/usb.h>

namespace usb {

class Dfu;
using DfuBase = ddk::Device<Dfu, ddk::Messageable<fuchsia_hardware_usb_fwloader::Device>::Mixin>;

class Dfu : public DfuBase, public ddk::EmptyProtocol<ZX_PROTOCOL_USB_FWLOADER> {
 public:
  Dfu(zx_device_t* parent, const usb_protocol_t& usb, uint8_t intf_num,
      const usb_dfu_func_desc_t& func_desc)
      : DfuBase(parent), usb_(usb), intf_num_(intf_num), func_desc_(func_desc) {}

  // Spawns device node based on parent node.
  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease() { delete this; }

  // FIDL message implementation.
  void LoadPrebuiltFirmware(LoadPrebuiltFirmwareRequestView request,
                            LoadPrebuiltFirmwareCompleter::Sync& completer);
  void LoadFirmware(LoadFirmwareRequestView request, LoadFirmwareCompleter::Sync& completer);

 private:
  zx_status_t Bind();

  // Sends a USB control request to the device, and resets the control endpoint if stalled.
  // Returns ZX_OK if the request succeeded.
  zx_status_t ControlReq(uint8_t dir, uint8_t request, uint16_t value, void* data, size_t length,
                         size_t* out_length);

  // Downloads the data block to the device.
  // |block_num| should be incremented each time a block is transferred and range
  // from 0 to 65,535, wrapping around if necessary.
  // |len_to_write| is limited by the device max transfer size stored in |func_desc_|.
  zx_status_t Download(uint16_t block_num, uint8_t* buf, size_t len_to_write);
  // Stores the status data of the last download transfer into |out_status|.
  zx_status_t GetStatus(usb_dfu_get_status_data_t* out_status);
  // Sets the device status to OK and transitions the device to the DFU Idle state.
  zx_status_t ClearStatus();
  // Stores the current state of the device to |out_dfu_state|.
  zx_status_t GetState(uint8_t* out_dfu_state);

  const usb_protocol_t usb_;

  const uint8_t intf_num_;
  const usb_dfu_func_desc_t func_desc_;
};

}  // namespace usb

#endif  // SRC_DEVICES_USB_DRIVERS_USB_DFU_USB_DFU_H_
