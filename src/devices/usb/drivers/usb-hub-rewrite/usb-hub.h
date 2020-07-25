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
        ddk_interaction_loop_(&kAsyncLoopConfigNeverAttachToThread),
        ddk_interaction_executor_(ddk_interaction_loop_.dispatcher()) {}

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

  // Synchronously resets a port
  zx_status_t UsbHubInterfaceResetPort(uint32_t port);

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);

  void DdkRelease();

  ~UsbHubDevice();

 private:
  inspect::Inspector inspector_;
  usb_speed_t speed_;
  usb_endpoint_descriptor_t interrupt_endpoint_;
  ddk::UsbProtocolClient usb_;
  ddk::UsbBusProtocolClient bus_;
  async::Loop ddk_interaction_loop_;

  // Pending DDK callbacks that need to be ran on the dedicated DDK interaction thread
  async::Executor ddk_interaction_executor_;
};

}  // namespace usb_hub

#endif  // SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
