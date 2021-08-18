// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
#define SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
#include <fuchsia/hardware/usb/bus/cpp/banjo.h>
#include <fuchsia/hardware/usb/cpp/banjo.h>
#include <fuchsia/hardware/usb/hub/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/sync/completion.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/usb.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/hard_int.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/null_lock.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace usb_hub {

// Number of requests to pre-allocate
constexpr auto kPreallocatedRequestCount = 7;

// Maximum length of a control request.
constexpr auto kMaxRequestLength = 32;

// Hub Status Bit
constexpr auto hubStatusBit = 1;

DEFINE_HARD_INT(PortNumber, uint8_t)
DEFINE_HARD_INT(PortArrayIndex, uint8_t)

struct PortStatus : public fbl::DoublyLinkedListable<PortStatus*> {
  uint16_t status = 0;
  bool connected = false;
  bool reset_pending = false;
  bool enumeration_pending = false;
  bool link_active = false;
  usb_speed_t GetSpeed(usb_speed_t hub_speed) const;
  void Reset();
};

class UsbHubDevice;
using UsbHub = ddk::Device<UsbHubDevice, ddk::Unbindable, ddk::Initializable, ddk::GetProtocolable>;
using Request = usb::Request<void>;
using CallbackRequest = usb::CallbackRequest<sizeof(std::max_align_t) * 4>;
class UsbHubDevice : public UsbHub, public ddk::UsbHubInterfaceProtocol<UsbHubDevice> {
 public:
  explicit UsbHubDevice(zx_device_t* parent)
      : UsbHub(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  UsbHubDevice(zx_device_t* parent, std::unique_ptr<fpromise::executor> executor)
      : UsbHub(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        executor_(std::move(executor)) {}

  ~UsbHubDevice() { loop_.Shutdown(); }

  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out) {
    switch (proto_id) {
      case ZX_PROTOCOL_USB:
        usb_.GetProto(static_cast<usb_protocol_t*>(out));
        return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  // Synchronously resets a port
  zx_status_t UsbHubInterfaceResetPort(uint32_t port);

  // Retrieves the status of a port.
  zx::status<usb_port_status_t> GetPortStatus(PortNumber port);

  zx_status_t SetFeature(uint8_t request_type, uint16_t feature, uint16_t index);

  zx_status_t ClearFeature(uint8_t request_type, uint16_t feature, uint16_t index);

  void DdkInit(ddk::InitTxn txn);

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  static zx_status_t Bind(std::unique_ptr<fpromise::executor> executor, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn);

  void DdkRelease() { delete this; }

  zx::status<usb_hub_descriptor_t> GetUsbHubDescriptor(uint16_t type) {
    size_t length = sizeof(usb_hub_descriptor_t);
    auto result = ControlIn(USB_TYPE_CLASS | USB_RECIP_DEVICE | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                            static_cast<uint16_t>(type << 8), 0, length);
    if (result.is_error()) {
      return zx::error(result.error_value());
    }
    size_t request_size = result.value().size();
    usb_hub_descriptor_t hub_descriptor;
    if (sizeof(hub_descriptor) < request_size) {
      zxlogf(ERROR, "Size of hub descriptor less than request size");
      return zx::error(ZX_ERR_NO_MEMORY);
    }
    memcpy(&hub_descriptor, result.value().data(), request_size);
    auto* usb_descriptor = reinterpret_cast<usb_descriptor_header_t*>(&hub_descriptor);
    if (usb_descriptor->b_length != request_size) {
      zxlogf(ERROR, "Mismatched descriptor length");
      return zx::error(ZX_ERR_BAD_STATE);
    }
    return zx::ok(hub_descriptor);
  }

 private:
  zx_status_t Init();

  // Powers on all ports on the hub
  zx_status_t PowerOnPorts();

  void InterruptCallback();

  // Updates the status of all ports on the hub
  zx_status_t GetPortStatus();

  void HandlePortStatusChanged(PortNumber port) __TA_REQUIRES(async_execution_context_);

  // Starts the interrupt loop
  zx_status_t StartInterruptLoop();

  // Resets a port
  zx_status_t ResetPort(PortNumber port);

  // Obtains the 1-based port number from a PortStatus reference
  PortNumber GetPortNumber(const PortStatus& status);

  // Begins enumeration of the next device on the hub
  void EnumerateNext() __TA_REQUIRES(async_execution_context_);

  // Starts the enumeration process for a specified port number
  void BeginEnumeration(PortNumber port);

  // Invoked when a device is attached to the hub
  void HandleDeviceConnected(PortNumber port) __TA_REQUIRES(async_execution_context_);

  // Invoked when a device is disconnected from the hub
  void HandleDeviceDisconnected(PortNumber port) __TA_REQUIRES(async_execution_context_);

  // Invoked when a device finishes resetting. Not called when invoked from usb-fwloader.
  void HandleResetComplete(PortNumber port) __TA_REQUIRES(async_execution_context_);

  PortArrayIndex PortNumberToIndex(PortNumber port) {
    fbl::AutoLock lock(&async_execution_context_);
    // NOTE -- This read is safe even if not done from async context
    // since size() is expected to be constant after initialization.
    ZX_ASSERT((port.value() > 0) && (port.value() <= port_status_.size()));
    return PortArrayIndex(port.value() - 1);
  }

  PortNumber IndexToPortNumber(PortArrayIndex index) {
    fbl::AutoLock lock(&async_execution_context_);
    // NOTE -- This read is safe even if not done from async context
    // since size() is expected to be constant after initialization.
    ZX_ASSERT((index.value() >= 0) && (index.value() < port_status_.size()));
    return PortNumber(index.value() + 1);
  }

  zx::status<std::vector<uint8_t>> ControlIn(uint8_t request_type, uint8_t request, uint16_t value,
                                             uint16_t index, size_t read_size);

  zx_status_t ControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                         const void* write_buffer, size_t write_size);

  std::optional<Request> AllocRequest();

  fpromise::promise<Request, void> RequestQueue(Request request);

  std::atomic<bool> shutting_down_ = false;
  fbl::Array<PortStatus> port_status_ __TA_GUARDED(async_execution_context_);
  fbl::DoublyLinkedList<PortStatus*> pending_enumeration_list_
      __TA_GUARDED(async_execution_context_);
  usb_hub_descriptor_t hub_descriptor_;
  usb::RequestPool<void> request_pool_;
  inspect::Inspector inspector_;
  usb_speed_t speed_;
  usb_endpoint_descriptor_t interrupt_endpoint_;
  ddk::UsbProtocolClient usb_;
  ddk::UsbBusProtocolClient bus_;
  async::Loop loop_;
  std::unique_ptr<fpromise::executor> executor_;
  thrd_t callback_thread_;
  sync_completion_t xfer_done_;

  std::optional<ddk::InitTxn> txn_;
  fbl::NullLock async_execution_context_;
};

}  // namespace usb_hub

#endif  // SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
