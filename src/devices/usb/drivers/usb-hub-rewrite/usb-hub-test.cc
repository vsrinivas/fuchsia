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

#include <ddktl/device.h>
#include <ddktl/protocol/usb.h>
#include <ddktl/protocol/usb/bus.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <usb/usb-request.h>
#include <zxtest/zxtest.h>

#include "ddk/protocol/usb/hub.h"
#include "ddktl/protocol/usb/hub.h"

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

  void UsbRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb) {}

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

}  // namespace
