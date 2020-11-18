// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>

#include <ddktl/protocol/usb.h>
#include <usb/request-cpp.h>
#include <zxtest/base/test.h>
#include <zxtest/zxtest.h>

#include "src/devices/usb/testing/descriptor-builder/descriptor-builder.h"

namespace {
using Request = usb::Request<void>;
using UnownedRequest = usb::BorrowedRequest<void>;
using UnownedRequestQueue = usb::BorrowedRequestQueue<void>;

class FakeDevice : public ddk::UsbProtocol<FakeDevice> {
 public:
  FakeDevice() {
    usb::DeviceDescriptorBuilder dev_builder;
    usb::ConfigurationBuilder config_builder(0);
    usb::InterfaceBuilder interface_builder(0);
    usb::EndpointBuilder in_endpoint_builder(0, USB_ENDPOINT_BULK, 0, true);
    usb::EndpointBuilder out_endpoint_builder(0, USB_ENDPOINT_BULK, 0, false);
    usb::EndpointBuilder int_endpoint_builder(1, USB_ENDPOINT_INTERRUPT, 0, true);
    interface_builder.AddEndpoint(in_endpoint_builder);
    interface_builder.AddEndpoint(out_endpoint_builder);
    interface_builder.AddEndpoint(int_endpoint_builder);
    config_builder.AddInterface(interface_builder);
    dev_builder.AddConfiguration(config_builder);
    descriptor_ = dev_builder.Generate();
  }

  void Unplug() {
    UnownedRequestQueue queue;
    {
      fbl::AutoLock<fbl::Mutex> lock(&mutex_);
      unplugged = true;
      queue = std::move(queue_);
    }
    queue.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
    dev_ops_.unbind(dev_context_);
    dev_ops_.release(dev_context_);
  }

  usb_protocol_t proto() const {
    usb_protocol_t proto;
    proto.ctx = const_cast<FakeDevice*>(this);
    proto.ops = const_cast<usb_protocol_ops_t*>(&usb_protocol_ops_);
    return proto;
  }
  zx_status_t UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                            int64_t timeout, const void* write_buffer, size_t write_size) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           int64_t timeout, void* out_read_buffer, size_t read_size,
                           size_t* out_read_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void UsbRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb) {
    fbl::AutoLock<fbl::Mutex> lock(&mutex_);
    if (unplugged) {
      lock.release();
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
  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, void* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  size_t UsbGetDescriptorsLength() { return descriptor_.size(); }
  void UsbGetDescriptors(void* out_descs_buffer, size_t descs_size, size_t* out_descs_actual) {
    ZX_ASSERT(descs_size == descriptor_.size());
    memcpy(out_descs_buffer, descriptor_.data(), descs_size);
  }
  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     void* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbCancelAll(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }
  uint64_t UsbGetCurrentFrame() { return 0; }
  size_t UsbGetRequestSize() { return UnownedRequest::RequestSize(sizeof(usb_request_t)); }
  void SetDevOps(void* ctx, zx_protocol_device_t ops) {
    dev_context_ = ctx;
    dev_ops_ = ops;
  }

 private:
  fbl::Mutex mutex_;
  bool unplugged __TA_GUARDED(mutex_) = false;
  UnownedRequestQueue queue_ __TA_GUARDED(mutex_);
  void* dev_context_;
  zx_protocol_device_t dev_ops_;
  std::vector<uint8_t> descriptor_;
};

class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
    auto context = reinterpret_cast<const FakeDevice*>(device);
    if (proto_id == ZX_PROTOCOL_USB) {
      *reinterpret_cast<usb_protocol_t*>(protocol) = context->proto();
      return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }
  zx_status_t DeviceRemove(zx_device_t* device) { return ZX_OK; }

  void DeviceInitReply(zx_device_t* device, zx_status_t status,
                       const device_init_reply_args_t* args) {}

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) {
    auto context = reinterpret_cast<FakeDevice*>(parent);
    context->SetDevOps(args->ctx, *args->ops);
    return ZX_OK;
  }
};

extern "C" {
zx_status_t hci_bind(void* ctx, zx_device_t* device);
}

class BTHarness : public zxtest::Test {
 public:
  void SetUp() override {
    ctx = std::make_unique<FakeDevice>();
    ASSERT_OK(hci_bind(nullptr, reinterpret_cast<zx_device_t*>(ctx.get())));
  }

  void TearDown() override { ctx->Unplug(); }
  std::unique_ptr<FakeDevice> ctx;

 private:
  Binder bind;
};

TEST_F(BTHarness, DoesNotCrash) {}
}  // namespace
