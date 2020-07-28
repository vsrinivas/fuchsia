// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
#define SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/zx/status.h>
#include <zircon/hw/usb.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb.h>
#include <ddktl/protocol/usb/bus.h>
#include <ddktl/protocol/usb/hub.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace usb_hub {

// Number of requests to pre-allocate
constexpr auto kPreallocatedRequestCount = 7;

// Maximum length of a control request.
constexpr auto kMaxRequestLength = 32;

template <typename T>
struct VariableLengthDescriptor {
  T descriptor;
  size_t length;
};

class UsbHubDevice;
using UsbHub =
    ddk::Device<UsbHubDevice, ddk::UnbindableNew, ddk::Initializable, ddk::GetProtocolable>;
using Request = usb::Request<void>;
class UsbHubDevice : public UsbHub, public ddk::UsbHubInterfaceProtocol<UsbHubDevice> {
 public:
  explicit UsbHubDevice(zx_device_t* parent)
      : UsbHub(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        executor_(loop_.dispatcher()),
        blocking_executor_(loop_.dispatcher()) {}

  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out) {
    switch (proto_id) {
      case ZX_PROTOCOL_USB:
        usb_.GetProto(static_cast<usb_protocol_t*>(out));
        return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  void DdkInit(ddk::InitTxn txn);

  zx_status_t Init();

  // Invokes a promise and waits for its completion
  zx_status_t RunSynchronously(fit::promise<void, zx_status_t> promise);

  // Synchronously resets a port
  zx_status_t UsbHubInterfaceResetPort(uint32_t port);

  // Retrieves the status of a port.
  fit::promise<usb_port_status_t, zx_status_t> GetPortStatus(uint8_t port);

  // Invokes a promise at the specified deadline
  fit::promise<void, zx_status_t> Sleep(zx::time deadline);

  fit::promise<void, zx_status_t> SetFeature(uint8_t request_type, uint16_t feature,
                                             uint16_t index);

  fit::promise<void, zx_status_t> ClearFeature(uint8_t request_type, uint16_t feature,
                                               uint16_t index);

  fit::promise<std::vector<uint8_t>, zx_status_t> ControlIn(uint8_t request_type, uint8_t request,
                                                            uint16_t value, uint16_t index,
                                                            size_t read_size);

  fit::promise<void, zx_status_t> ControlOut(uint8_t request_type, uint8_t request, uint16_t value,
                                             uint16_t index, const void* write_buffer,
                                             size_t write_size);

  std::optional<Request> AllocRequest();

  template <typename T>
  fit::promise<VariableLengthDescriptor<T>, zx_status_t> GetVariableLengthDescriptor(
      uint8_t request_type, uint16_t type, uint16_t index, size_t length = sizeof(T)) {
    static_assert(sizeof(T) >= sizeof(usb_descriptor_header_t));
    return ControlIn(request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                     static_cast<uint16_t>(type << 8 | index), 0, length)
        .and_then([](std::vector<uint8_t>& data)
                      -> fit::result<VariableLengthDescriptor<T>, zx_status_t> {
          VariableLengthDescriptor<T> value;
          memcpy(&value.descriptor, data.data(), data.size());
          if (reinterpret_cast<usb_descriptor_header_t*>(&value.descriptor)->bLength !=
              data.size()) {
            zxlogf(INFO, "Mismatched descriptor length\n");
            return fit::error(ZX_ERR_BAD_STATE);
          }
          value.length = data.size();
          return fit::ok(value);
        });
  }

  template <typename T>
  fit::promise<T, zx_status_t> GetDescriptor(uint8_t request_type, uint16_t type, uint16_t index,
                                             size_t length = sizeof(T)) {
    return GetVariableLengthDescriptor<T>(request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                                          static_cast<uint16_t>(type << 8 | index), length)
        .and_then([length](VariableLengthDescriptor<T>& data) {
          if (data.length != length) {
            return fit::error(ZX_ERR_BAD_STATE);
          }
          return fit::ok(data.descriptor);
        });
  }

  fit::promise<Request, void> RequestQueue(Request request);

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);

  void DdkRelease();

  ~UsbHubDevice();

 private:
  usb::RequestPool<void> request_pool_;
  inspect::Inspector inspector_;
  usb_speed_t speed_;
  usb_endpoint_descriptor_t interrupt_endpoint_;
  ddk::UsbProtocolClient usb_;
  ddk::UsbBusProtocolClient bus_;
  async::Loop loop_;
  async::Executor executor_;

  // Pending DDK callbacks that need to be ran on the dedicated DDK interaction thread
  async::Executor blocking_executor_;
};

}  // namespace usb_hub

#endif  // SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
