// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/cpp/banjo.h>

#include <gtest/gtest.h>
#include <usb/request-cpp.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/devices/usb/testing/descriptor-builder/descriptor-builder.h"

// hci_bind is defined in bt-transport-usb.c
extern "C" {
zx_status_t hci_bind(void* ctx, zx_device_t* device);
}

namespace {

using Request = usb::Request<void>;
using UnownedRequest = usb::BorrowedRequest<void>;
using UnownedRequestQueue = usb::BorrowedRequestQueue<void>;

class FakeUsbDevice : public ddk::UsbProtocol<FakeUsbDevice> {
 public:
  FakeUsbDevice() {
    // Configure the USB endpoint configuration from Core Spec v5.3, Vol 4, Part B, Sec 2.1.1.

    // Interface 0 contains the bulk (ACL) and interrupt (HCI event) endpoints.
    usb::InterfaceBuilder interface_0_builder(/*config_num=*/0);
    usb::EndpointBuilder interrupt_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_INTERRUPT,
                                                    /*endpoint_index=*/1, /*in=*/true);
    interface_0_builder.AddEndpoint(interrupt_endpoint_builder);
    usb::EndpointBuilder bulk_in_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_BULK,
                                                  /*endpoint_index=*/0, /*in=*/true);
    interface_0_builder.AddEndpoint(bulk_in_endpoint_builder);
    bulk_in_addr_ = usb::EpIndexToAddress(usb::kInEndpointStart);
    usb::EndpointBuilder bulk_out_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_BULK,
                                                   /*endpoint_index=*/0, /*in=*/false);
    interface_0_builder.AddEndpoint(bulk_out_endpoint_builder);

    usb::ConfigurationBuilder config_builder(/*config_num=*/0);
    config_builder.AddInterface(interface_0_builder);

    usb::DeviceDescriptorBuilder dev_builder;
    dev_builder.AddConfiguration(config_builder);
    descriptor_ = dev_builder.Generate();
  }

  void Unplug() {
    unplugged = true;
    queue_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
  }

  usb_protocol_t proto() const {
    usb_protocol_t proto;
    proto.ctx = const_cast<FakeUsbDevice*>(this);
    proto.ops = const_cast<usb_protocol_ops_t*>(&usb_protocol_ops_);
    return proto;
  }
  zx_status_t UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                            int64_t timeout, const uint8_t* write_buffer, size_t write_size) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           int64_t timeout, uint8_t* out_read_buffer, size_t read_size,
                           size_t* out_read_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void UsbRequestQueue(usb_request_t* usb_request,
                       const usb_request_complete_callback_t* complete_cb) {
    if (unplugged) {
      usb_request_complete(usb_request, ZX_ERR_IO_NOT_PRESENT, 0, complete_cb);
      return;
    }
    UnownedRequest request(usb_request, *complete_cb, sizeof(usb_request_t));
    queue_.push(std::move(request));
  }

  usb_speed_t UsbGetSpeed() { return USB_SPEED_FULL; }

  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint8_t UsbGetConfiguration() { return 0; }
  zx_status_t UsbSetConfiguration(uint8_t configuration) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbResetEndpoint(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbResetDevice() { return ZX_ERR_NOT_SUPPORTED; }
  size_t UsbGetMaxTransferSize(uint8_t ep_address) { return 0; }
  uint32_t UsbGetDeviceId() { return 0; }
  void UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {
    memcpy(out_desc, descriptor_.data(), sizeof(usb_device_descriptor_t));
  }
  zx_status_t UsbGetConfigurationDescriptorLength(uint8_t configuration, size_t* out_length) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, uint8_t* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  size_t UsbGetDescriptorsLength() { return descriptor_.size(); }
  void UsbGetDescriptors(uint8_t* out_descs_buffer, size_t descs_size, size_t* out_descs_actual) {
    ZX_ASSERT(descs_size == descriptor_.size());
    memcpy(out_descs_buffer, descriptor_.data(), descs_size);
  }
  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     uint8_t* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbCancelAll(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }
  uint64_t UsbGetCurrentFrame() { return 0; }
  size_t UsbGetRequestSize() { return UnownedRequest::RequestSize(sizeof(usb_request_t)); }

  void StallOneBulkRequest() {
    std::optional<usb::BorrowedRequest<>> value;
    while (true) {
      value = queue_.pop();
      if (!value.has_value()) {
        break;
      }

      // If this is a bulk request, complete it with ZX_ERR_IO_INVALID, which indicates a stall.
      if (value->request()->header.ep_address == bulk_in_addr_) {
        value->Complete(ZX_ERR_IO_INVALID, 0);
        break;
      }

      // If this was not a bulk request, just put it back on the queue - we'll deal with it later.
      queue_.push(std::move(*value));
    }
  }

 private:
  bool unplugged = false;
  UnownedRequestQueue queue_;
  std::vector<uint8_t> descriptor_;
  uint8_t bulk_in_addr_;
};

class BtTransportUsbTest : public ::testing::Test {
 public:
  void SetUp() override {
    root_device_ = MockDevice::FakeRootParent();
    root_device_->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device_.proto().ops,
                              fake_usb_device_.proto().ctx);

    // bt-transport-usb doesn't use ctx.
    hci_bind(/*ctx=*/nullptr, root_device_.get());
    ASSERT_EQ(1u, root_device()->child_count());
    ASSERT_TRUE(dut());
  }
  void TearDown() override {
    // Complete USB requests with error status. This must be done before releasing DUT so that
    // release doesn't block on request completion.
    fake_usb_device_.Unplug();
    // DUT should call device_async_remove() in response to failed USB requests.
    EXPECT_EQ(dut()->WaitUntilAsyncRemoveCalled(), ZX_OK);

    dut()->UnbindOp();
    EXPECT_TRUE(dut()->UnbindReplyCalled());
    dut()->ReleaseOp();
  }

  // The root device that bt-transport-usb binds to.
  MockDevice* root_device() const { return root_device_.get(); }

  // Returns the MockDevice corresponding to the bt-transport-usb driver.
  MockDevice* dut() const { return root_device_->GetLatestChild(); }

  FakeUsbDevice* fake_usb() { return &fake_usb_device_; }

 private:
  std::shared_ptr<MockDevice> root_device_;
  FakeUsbDevice fake_usb_device_;
};

// This tests the test fixture setup and teardown.
TEST_F(BtTransportUsbTest, Lifecycle) {}

TEST_F(BtTransportUsbTest, IgnoresStalledRequest) {
  fake_usb()->StallOneBulkRequest();
  EXPECT_FALSE(dut()->RemoveCalled());
}

}  // namespace
