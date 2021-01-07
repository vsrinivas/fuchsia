// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_TESTS_USB_HCI_TEST_USB_HCI_TEST_DRIVER_H_
#define SRC_DEVICES_USB_TESTS_USB_HCI_TEST_USB_HCI_TEST_DRIVER_H_

#include <fuchsia/hardware/usb/cpp/banjo.h>
#include <fuchsia/hardware/usb/hcitest/llcpp/fidl.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <thread>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <usb/usb.h>

namespace usb {

class HciTest;
using HciTestBase = ddk::Device<HciTest, ddk::Unbindable, ddk::Messageable, ddk::Initializable>;

class HciTest : public HciTestBase,
                public ddk::EmptyProtocol<ZX_PROTOCOL_USB_HCI_TEST>,
                public llcpp::fuchsia::hardware::usb::hcitest::Device::Interface {
 public:
  HciTest(zx_device_t* parent, const ddk::UsbProtocolClient& usb)
      : HciTestBase(parent), usb_(usb) {}

  // Spawns device node based on parent node.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn) {
    if (test_thread_.has_value()) {
      if (test_thread_->joinable()) {
        test_thread_->join();
      }
    }
    txn.Reply();
  }
  void DdkRelease() { delete this; }
  void Run(RunCompleter::Sync& _completer);

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    llcpp::fuchsia::hardware::usb::hcitest::Device::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

  void DdkInit(ddk::InitTxn txn);
  void TestThread(RunCompleter::Async completer);
  void EnumerationThread(ddk::InitTxn txn);

 private:
  bool test_running_ = false;
  std::optional<std::thread> test_thread_;
  std::optional<std::thread> enumeration_thread_;
  static void request_complete(void* ctx, usb_request_t* request);
  zx_status_t Bind();
  usb_ss_ep_comp_descriptor_t bulk_out_3_ = {};
  usb_endpoint_descriptor_t bulk_out_ = {};
  usb_endpoint_descriptor_t irq_in_ = {};
  usb_ss_ep_comp_descriptor_t irq_in_3_ = {};
  usb_endpoint_descriptor_t isoch_in_ = {};
  usb_ss_ep_comp_descriptor_t isoch_in_3_ = {};
  usb_ss_ep_comp_descriptor_t bulk_in_3_ = {};
  usb_endpoint_descriptor_t bulk_in_ = {};
  const ddk::UsbProtocolClient usb_;
};

}  // namespace usb

#endif  // SRC_DEVICES_USB_TESTS_USB_HCI_TEST_USB_HCI_TEST_DRIVER_H_
