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
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <fbl/array.h>
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

template <typename T>
struct VariableLengthDescriptor {
  T descriptor;
  size_t length;
};

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
      : UsbHub(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        blocking_executor_(loop_.dispatcher()) {}

  UsbHubDevice(zx_device_t* parent, std::unique_ptr<fit::executor> executor)
      : UsbHub(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        executor_(std::move(executor)),
        blocking_executor_(loop_.dispatcher()) {}

  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out) {
    switch (proto_id) {
      case ZX_PROTOCOL_USB:
        usb_.GetProto(static_cast<usb_protocol_t*>(out));
        return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  zx_status_t Init();

  // Invokes a promise and waits for its completion
  zx_status_t RunSynchronously(fit::promise<void, zx_status_t> promise);

  // Synchronously resets a port
  zx_status_t UsbHubInterfaceResetPort(uint32_t port);

  // Powers on all ports on the hub
  fit::promise<void, zx_status_t> PowerOnPorts();

  // Retrieves the status of a port.
  fit::promise<usb_port_status_t, zx_status_t> GetPortStatus(PortNumber port);

  void InterruptCallback(CallbackRequest request);

  // Updates the status of all ports on the hub
  fit::promise<void, zx_status_t> GetPortStatus();

  void DdkInit(ddk::InitTxn txn);

  void HandlePortStatusChanged(PortNumber port) __TA_REQUIRES(async_execution_context_);

  // Starts the interrupt loop. The only way to exit is by invoking CancelAll.
  void StartInterruptLoop();

  // Resets a port
  fit::promise<void, zx_status_t> ResetPort(PortNumber port);

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

  // Runs blocking code (as specified by lambda) in the blocking
  // context. Returns the result of executing the lambda which
  // gets executed in the async context.
  template <typename T>
  auto RunBlocking(fit::function<T()> task) {
    fit::bridge<T, void> bridge;

    blocking_executor_.schedule_task(fit::make_promise(
        [functor = std::move(task), completer = std::move(bridge.completer)]() mutable {
          auto value = functor();
          completer.complete_ok(value);
        }));
    return std::move(bridge.consumer.promise());
  }

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

  static zx_status_t Bind(std::unique_ptr<fit::executor> executor, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn);

  void DdkRelease();

  ~UsbHubDevice();

 private:
  std::atomic_bool request_pending_ = false;
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
  std::unique_ptr<fit::executor> executor_;

  std::optional<ddk::InitTxn> txn_;
  // Executor for running blocking tasks. These tasks MUST NOT
  // interact with state that is mutated by the executor_
  // or undefined behavior may occur.
  async::Executor blocking_executor_;
  fbl::NullLock async_execution_context_;
};

}  // namespace usb_hub

#endif  // SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_USB_HUB_H_
