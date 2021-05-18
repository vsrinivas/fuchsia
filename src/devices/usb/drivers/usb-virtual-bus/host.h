// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_HOST_H_
#define SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_HOST_H_

#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/virtualbustest/llcpp/fidl.h>
#include <lib/ddk/device.h>

#include <thread>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace virtualbus {

class Device;
using DeviceType =
    ddk::Device<Device, ddk::Unbindable,
                ddk::Messageable<fuchsia_hardware_usb_virtualbustest::BusTest>::Mixin>;
class Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_VIRTUALBUS_TEST> {
 public:
  explicit Device(zx_device_t* parent) : DeviceType(parent), usb_client_(parent) {}
  ~Device();

  zx_status_t Bind();

  void RunTest();
  void DdkUnbind(ddk::UnbindTxn txn);

  void DdkRelease();
  static zx_status_t Bind(zx_device_t* device);
  void RunShortPacketTest(RunShortPacketTestRequestView request,
                          RunShortPacketTestCompleter::Sync& completer) override;

 private:
  void RequestComplete(usb_request_t* request);

  ddk::UsbProtocolClient usb_client_ = {};
  std::optional<RunShortPacketTestCompleter::Async> completer_;

  bool enabled_ = false;

  size_t parent_req_size_ = 0;
  uint8_t bulk_out_addr_ = 0;

  std::thread cancel_thread_;
};

}  // namespace virtualbus

#endif  // SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_HOST_H_
