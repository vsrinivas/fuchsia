// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_HOST_H_
#define SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_HOST_H_

#include <fuchsia/hardware/usb/virtualbustest/llcpp/fidl.h>

#include <thread>

#include <ddk/device.h>
#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace virtualbus {

constexpr auto kVid = 0x18D1;
constexpr auto kDid = 0x2;

class Device;
using DeviceType = ddk::Device<Device, ddk::UnbindableNew, ddk::Messageable>;
class Device : public DeviceType,
               public ::llcpp::fuchsia::hardware::usb::virtualbustest::BusTest::Interface,
               public ddk::EmptyProtocol<ZX_PROTOCOL_VIRTUALBUS_TEST> {
 public:
  explicit Device(zx_device_t* parent) : DeviceType(parent), usb_client_(parent) {}
  ~Device();

  zx_status_t Bind();

  void RunTest();
  void DdkUnbindNew(ddk::UnbindTxn txn);

  void DdkRelease();
  static zx_status_t Bind(zx_device_t* device);
  void RunShortPacketTest(RunShortPacketTestCompleter::Sync completer);

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::hardware::usb::virtualbustest::BusTest::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

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
