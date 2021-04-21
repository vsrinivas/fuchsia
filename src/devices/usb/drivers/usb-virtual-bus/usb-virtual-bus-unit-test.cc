// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/dci/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "usb-virtual-bus.h"

namespace usb_virtual_bus {

TEST(VirtualBusUnitTest, DdkLifecycle) {
  fake_ddk::Bind ddk;

  auto bus = new UsbVirtualBus(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(bus);

  ASSERT_OK(bus->DdkAdd("usb-virtual-bus"));
  ASSERT_OK(ddk.WaitUntilInitComplete());

  bus->DdkAsyncRemove();
  // Check that unbind has replied.
  ASSERT_OK(ddk.WaitUntilRemove());
  ASSERT_TRUE(ddk.Ok());

  // This will join with the device thread and delete the bus object.
  bus->DdkRelease();
}

class FakeDci : public ddk::UsbDciInterfaceProtocol<FakeDci> {
 public:
  explicit FakeDci()
      : UsbDciInterfaceProtocol(), protocol_{&usb_dci_interface_protocol_ops_, this} {}

  usb_dci_interface_protocol_t* get_proto() { return &protocol_; }

  // UsbDciInterface implementation.
  // This will block until the test calls |CompleteControlRequest|.
  zx_status_t UsbDciInterfaceControl(const usb_setup_t* setup, const uint8_t* write_buffer,
                                     size_t write_size, uint8_t* out_read_buffer, size_t read_size,
                                     size_t* out_read_actual) {
    sync_completion_signal(&control_start_sync_);
    sync_completion_wait(&control_complete_sync_, ZX_TIME_INFINITE);
    return ZX_OK;
  }

  // Blocks until FakeDci has received the control request.
  void WaitForControlRequestStart() {
    sync_completion_wait(&control_start_sync_, ZX_TIME_INFINITE);
  }

  // Signals FakeDci to complete the control request.
  void CompleteControlRequest() { sync_completion_signal(&control_complete_sync_); }

  void UsbDciInterfaceSetConnected(bool connected) {}
  void UsbDciInterfaceSetSpeed(usb_speed_t speed) {}

 private:
  usb_dci_interface_protocol_t protocol_;
  sync_completion_t control_start_sync_;
  sync_completion_t control_complete_sync_;
};

// Tests unbinding the usb virtual bus while a control request is in progress.
TEST(VirtualBusUnitTest, UnbindDuringControlRequest) {
  fake_ddk::Bind ddk;

  auto bus = new UsbVirtualBus(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(bus);

  ASSERT_OK(bus->DdkAdd("usb-virtual-bus"));
  ASSERT_OK(ddk.WaitUntilInitComplete());

  // This needs to be true, otherwise requests will fail to be queued.
  bus->SetConnected(true);

  FakeDci fake_dci;
  ASSERT_OK(bus->UsbDciSetInterface(fake_dci.get_proto()));

  // This will be signalled by the control request completion callback.
  sync_completion_t usb_req_sync;
  // Start the control request before unbinding the device.
  // Do this in a new thread as it is a blocking operation, and we will not
  // request it be completed until after we begin unbinding.
  std::thread req_thread([&] {
    usb_request_complete_callback_t callback = {
        .callback =
            [](void* ctx, usb_request_t* req) {
              sync_completion_t* sync = static_cast<sync_completion_t*>(ctx);
              sync_completion_signal(sync);
              usb_request_release(req);
            },
        .ctx = &usb_req_sync,
    };
    size_t parent_req_size = bus->UsbHciGetRequestSize();
    usb_request_t* fake_req;
    ASSERT_OK(usb_request_alloc(&fake_req, PAGE_SIZE, 0 /* ep_address */, parent_req_size));

    bus->UsbHciRequestQueue(fake_req, &callback);
  });

  fake_dci.WaitForControlRequestStart();

  // Request the device begin unbinding.
  // This should wake up the worker thread, which will block until the control request completes.
  bus->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  fake_dci.CompleteControlRequest();

  // Wait for the control request to complete.
  sync_completion_wait(&usb_req_sync, ZX_TIME_INFINITE);
  req_thread.join();

  // Check that unbind has replied.
  ASSERT_OK(ddk.WaitUntilRemove());
  ASSERT_TRUE(ddk.Ok());

  // This will join with the device thread and delete the bus object.
  bus->DdkRelease();
}

}  // namespace usb_virtual_bus
