// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hub.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/hub.h>
#include <zircon/process.h>
#include <zircon/time.h>

#include <array>
#include <atomic>
#include <cstring>
#include <variant>

#include <ddk/protocol/usb/hub.h>
#include <ddk/protocol/usb/request.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb.h>
#include <ddktl/protocol/usb/bus.h>
#include <ddktl/protocol/usb/hub.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <usb/usb-request.h>
#include <zxtest/zxtest.h>

namespace {

// Raw descriptor from SMAYS hub obtained via USB packet capture.
const uint8_t kSmaysHubDescriptor[] = {9, 2, 25, 0, 1, 1, 0, 224, 50, 9, 4, 0, 0,
                                       1, 9, 0,  0, 0, 7, 5, 129, 3,  1, 0, 12};

// Hub to emulate
enum class EmulationMode {
  // SMAYS OTG hub
  Smays = 0,
};

struct EmulationMetadata {
  fbl::Span<const uint8_t> descriptor;
  EmulationMode mode;
  void SetProfile(EmulationMode mode) {
    this->mode = mode;
    switch (mode) {
      case EmulationMode::Smays:
        descriptor = fbl::Span(kSmaysHubDescriptor, sizeof(kSmaysHubDescriptor));
        break;
    }
  }
};

class FakeDevice;
class FakeDevice : public ddk::UsbBusProtocol<FakeDevice>, public ddk::UsbProtocol<FakeDevice> {
 public:
  explicit FakeDevice(EmulationMode mode) : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    emulation_.SetProfile(mode);
    loop_.StartThread();
  }

  void SetOpTable(const zx_protocol_device_t* ops_table, void* ctx) {
    ops_table_ = ops_table;
    ctx_ = ctx;
  }

  void ClearOpTable() { ops_table_ = nullptr; }

  void Unbind() { ops_table_->unbind(ctx_); }

  void Release() {
    ops_table_->release(ctx_);
    ops_table_ = nullptr;
  }

  // USB Bus protocol implementation.
  zx_status_t UsbBusConfigureHub(zx_device_t* hub_device, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt) {
    return ZX_OK;
  }

  zx_status_t UsbBusDeviceAdded(zx_device_t* hub_device, uint32_t port, usb_speed_t speed) {
    return ZX_OK;
  }

  zx_status_t UsbBusDeviceRemoved(zx_device_t* hub_device, uint32_t port) { return ZX_OK; }

  zx_status_t UsbBusSetHubInterface(zx_device_t* usb_device,
                                    const usb_hub_interface_protocol_t* hub) {
    return ZX_OK;
  }

  // USB protocol implementation
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
    if (request_callback_.has_value()) {
      (*request_callback_)(usb_request, *complete_cb);
    }
  }

  usb_speed_t UsbGetSpeed() { return USB_SPEED_HIGH; }

  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t UsbGetConfiguration() { return 0; }

  zx_status_t UsbSetConfiguration(uint8_t configuration) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    interrupt_endpoint_ = ep_desc->bEndpointAddress;
    return ZX_OK;
  }
  zx_status_t UsbResetEndpoint(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbResetDevice() { return ZX_ERR_NOT_SUPPORTED; }

  size_t UsbGetMaxTransferSize(uint8_t ep_address) { return 0; }

  uint32_t UsbGetDeviceId() { return 0; }

  void UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {}

  zx_status_t UsbGetConfigurationDescriptorLength(uint8_t configuration, size_t* out_length) {
    return 0;
  }

  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, void* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  size_t UsbGetDescriptorsLength() { return emulation_.descriptor.size(); }

  void UsbGetDescriptors(void* out_descs_buffer, size_t descs_size, size_t* out_descs_actual) {
    memcpy(out_descs_buffer, emulation_.descriptor.data(), emulation_.descriptor.size());
    *out_descs_actual = emulation_.descriptor.size();
  }

  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     void* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbCancelAll(uint8_t ep_address) { return ZX_OK; }

  uint64_t UsbGetCurrentFrame() { return 0; }

  size_t UsbGetRequestSize() { return sizeof(usb_request_t); }

  usb_hub::UsbHubDevice* device() { return static_cast<usb_hub::UsbHubDevice*>(ctx_); }

  void SetRequestCallback(fit::function<void(usb_request_t*, usb_request_complete_t)> callback) {
    request_callback_ = std::move(callback);
  }

  bool HasOps() { return ops_table_; }

  zx_status_t GetProtocol(uint32_t proto, void* protocol) {
    switch (proto) {
      case ZX_PROTOCOL_USB: {
        auto proto = static_cast<usb_protocol_t*>(protocol);
        proto->ctx = this;
        proto->ops = &usb_protocol_ops_;
      }
        return ZX_OK;
      case ZX_PROTOCOL_USB_BUS: {
        auto proto = static_cast<usb_bus_protocol_t*>(protocol);
        proto->ctx = this;
        proto->ops = &usb_bus_protocol_ops_;
      }
        return ZX_OK;
      default:
        return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
    }
  }

 private:
  std::optional<fit::function<void(usb_request_t*, usb_request_complete_t)>> request_callback_;
  async::Loop loop_;
  uint8_t interrupt_endpoint_ = 0;
  const zx_protocol_device_t* ops_table_ = nullptr;
  void* ctx_;
  EmulationMetadata emulation_;
};

template <EmulationMode mode>
class UsbHarness : public zxtest::Test {
 public:
  void SetUp() override {
    device_.emplace(mode);
    usb_hub::UsbHubDevice::Bind(nullptr, reinterpret_cast<zx_device_t*>(&device_.value()));
    ASSERT_TRUE(device_->HasOps());
  }

  usb_hub::UsbHubDevice* device() { return device_->device(); }

  void SetRequestCallback(fit::function<void(usb_request_t*, usb_request_complete_t)> callback) {
    device_->SetRequestCallback(std::move(callback));
  }

  void TearDown() override {
    device_->Unbind();
    device_->Release();
    ASSERT_FALSE(device_->HasOps());
  }

 private:
  std::optional<FakeDevice> device_;
};

using SmaysHarness = UsbHarness<EmulationMode::Smays>;

class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    auto context = const_cast<FakeDevice*>(reinterpret_cast<const FakeDevice*>(device));
    return context->GetProtocol(proto_id, protocol);
  }
  void SetRequestCallback(fit::function<void(usb_request_t*, sync_completion_t)> callback) {}
  zx_status_t DeviceRemove(zx_device_t* device) override { return ZX_OK; }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    auto context = reinterpret_cast<FakeDevice*>(parent);
    parent_ = context;
    context->SetOpTable(args->ops, args->ctx);
    return fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
  }

 private:
  FakeDevice* parent_;
};

static Binder bind;

TEST_F(SmaysHarness, BindTest) { ASSERT_OK(bind.WaitUntilInitComplete()); }

TEST_F(SmaysHarness, Timeout) {
  auto dev = device();
  zx::time start = zx::clock::get_monotonic();
  zx::time timeout = zx::deadline_after(zx::msec(30));
  bool ran = false;
  ASSERT_OK(dev->RunSynchronously(dev->Sleep(timeout).and_then([&]() {
    ASSERT_GT((zx::clock::get_monotonic() - start).to_msecs(), 29);
    ran = true;
  })));
  ASSERT_TRUE(ran);
}

/*
TEST_F(SmaysHarness, SetFeature) {
  auto dev = device();
  bool ran = false;
  SetRequestCallback([&](usb_request_t* request, usb_request_complete_t completion) {
    ASSERT_EQ(request->setup.bmRequestType, 3);
    ASSERT_EQ(request->setup.bRequest, USB_REQ_SET_FEATURE);
    ASSERT_EQ(request->setup.wIndex, 2);
    ran = true;
    usb_request_complete(request, ZX_OK, 0, &completion);
  });
  ASSERT_OK(dev->RunSynchronously(dev->SetFeature(3, 7, 2)));
  ASSERT_TRUE(ran);
}

TEST_F(SmaysHarness, ClearFeature) {
  auto dev = device();
  bool ran = false;
  SetRequestCallback([&](usb_request_t* request, usb_request_complete_t completion) {
    ASSERT_EQ(request->setup.bmRequestType, 3);
    ASSERT_EQ(request->setup.bRequest, USB_REQ_CLEAR_FEATURE);
    ASSERT_EQ(request->setup.wIndex, 2);
    ran = true;
    usb_request_complete(request, ZX_OK, 0, &completion);
  });
  ASSERT_OK(dev->RunSynchronously(dev->ClearFeature(3, 7, 2)));
  ASSERT_TRUE(ran);
}

TEST_F(SmaysHarness, GetPortStatus) {
  auto dev = device();
  // Run through all 127 permutations of port configuration states
  // and ensure we set the correct bits for each one.
  for (uint16_t i = 0; i < 127; i++) {
    bool ran = false;
    uint16_t features_cleared = 0;
    SetRequestCallback([&](usb_request_t* request, usb_request_complete_t completion) {
      switch (request->setup.bmRequestType) {
        case USB_RECIP_PORT | USB_DIR_IN: {
          usb_port_status_t* stat;
          usb_request_mmap(request, reinterpret_cast<void**>(&stat));
          stat->wPortChange = i;
          usb_request_complete(request, ZX_OK, sizeof(usb_port_status_t), &completion);
          return;
        } break;
        case USB_RECIP_PORT | USB_DIR_OUT: {
          switch (request->setup.wValue) {
            case USB_FEATURE_C_PORT_CONNECTION:
              features_cleared |= USB_C_PORT_CONNECTION;
              break;
            case USB_FEATURE_C_PORT_ENABLE:
              features_cleared |= USB_C_PORT_ENABLE;
              break;
            case USB_FEATURE_C_PORT_SUSPEND:
              features_cleared |= USB_C_PORT_SUSPEND;
              break;
            case USB_FEATURE_C_PORT_OVER_CURRENT:
              features_cleared |= USB_C_PORT_OVER_CURRENT;
              break;
            case USB_FEATURE_C_PORT_RESET:
              features_cleared |= USB_C_PORT_RESET;
              break;
            case USB_FEATURE_C_BH_PORT_RESET:
              features_cleared |= USB_C_BH_PORT_RESET;
              break;
            case USB_FEATURE_C_PORT_LINK_STATE:
              features_cleared |= USB_C_PORT_LINK_STATE;
              break;
            case USB_FEATURE_C_PORT_CONFIG_ERROR:
              features_cleared |= USB_C_PORT_CONFIG_ERROR;
              break;
          }
        } break;
        default:
          ASSERT_TRUE(false);
      }
      usb_request_complete(request, ZX_OK, 0, &completion);
    });
    ASSERT_OK(dev->RunSynchronously(
        dev->GetPortStatus(static_cast<uint8_t>(i)).and_then([&](usb_port_status_t& port_status) {
          ran = true;
          ASSERT_EQ(port_status.wPortChange, i);
        })));
    ASSERT_TRUE(ran);
    ASSERT_EQ(features_cleared, i);
  }
}

TEST_F(SmaysHarness, BadDescriptorTest) {
  auto dev = device();
  SetRequestCallback([&](usb_request_t* request, usb_request_complete_t completion) {
    usb_device_descriptor_t* devdesc;
    usb_request_mmap(request, reinterpret_cast<void**>(&devdesc));
    devdesc->bLength = sizeof(usb_device_descriptor_t);
    usb_request_complete(request, ZX_OK, sizeof(usb_descriptor_header_t), &completion);
  });
  ASSERT_EQ(dev->RunSynchronously(
                dev->GetVariableLengthDescriptor<usb_device_descriptor_t>(0, 0, 0).and_then(
                    [=](usb_hub::VariableLengthDescriptor<usb_device_descriptor_t>& descriptor) {
                      return fit::ok();
                    })),
            ZX_ERR_BAD_STATE);
}*/

}  // namespace
