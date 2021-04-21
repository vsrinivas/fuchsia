// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_FAKE_DEVICE_H_
#define SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_FAKE_DEVICE_H_

enum class OperationType {
  kSetOpTable,
  kUnbind,
  kRelease,
  kUsbBusDeviceAdded,
  kUsbBusDeviceRemoved,
  kUsbBusSetHubInterface,
  kUsbRequestQueue,
  kUsbEnableEndpoint,
  kUsbCancelAll,
  kHasOps,
  kConnectDevice,
  kDisconnectDevice,
  kResetPort,
  kResetPending,
  kInterrupt,
  kUnplug,
  kPowerOnEvent,
  kInitCompleteEvent,
  kDispatchInit,
  kExitEventLoop,
  kUnbindReplied,
};

class IOQueue;
struct IOEntry;
struct IOEntry : fbl::DoublyLinkedListable<std::unique_ptr<IOEntry>> {
  IOQueue* complete_queue;
  OperationType type;
  const zx_protocol_device_t* ops_table;
  uint8_t ep_address;
  void* ctx;
  usb_request_t* request;
  IOQueue* request_dispatch_queue;
  /* zx_device_t* */ uint64_t hub_device;
  uint32_t port;
  zx_status_t status;
  usb_hub_descriptor_t hub_desc;
  const usb_endpoint_descriptor_t* ep_desc;
  const usb_ss_ep_comp_descriptor_t* ss_com_desc;
  bool enable;
  usb_speed_t speed;
  usb_request_complete_callback_t completion;
  usb_hub_interface_protocol_t hub_interface;
  bool multi_tt;
  IOEntry(IOQueue* complete_queue, OperationType type)
      : complete_queue(complete_queue), type(type) {}
};

class IOQueue {
 public:
  void Insert(std::unique_ptr<IOEntry> entry) {
    fbl::AutoLock l(&mutex_);
    entries_.push_back(std::move(entry));
    event_.Broadcast();
  }
  template <typename Callback>
  void StartThread(Callback callback) {
    thread_.emplace(callback);
  }
  void Join() {
    thread_->join();
    thread_.reset();
  }
  std::unique_ptr<IOEntry> Wait() {
    fbl::AutoLock l(&mutex_);
    while (entries_.is_empty()) {
      event_.Wait(&mutex_);
    }
    return entries_.pop_front();
  }
  ~IOQueue() {
    if (thread_) {
      auto entry = std::make_unique<IOEntry>(nullptr, OperationType::kExitEventLoop);
      Insert(std::move(entry));
      thread_->join();
    }
  }

 private:
  std::optional<std::thread> thread_;
  fbl::Mutex mutex_;
  fbl::ConditionVariable event_ __TA_GUARDED(mutex_);
  fbl::DoublyLinkedList<std::unique_ptr<IOEntry>> entries_ __TA_GUARDED(mutex_);
};

// Raw descriptor from SMAYS hub obtained via USB packet capture.
const uint8_t kSmaysHubDescriptor[] = {9, 2, 25, 0, 1, 1, 0, 224, 50, 9, 4, 0, 0,
                                       1, 9, 0,  0, 0, 7, 5, 129, 3,  1, 0, 12};
const uint8_t kSmaysHubDescriptor2[] = {9, 41, 4, 0, 0, 50, 100, 0, 255};
const uint8_t kSmaysDeviceDescriptor[] = {18, 1, 0, 2,  9, 0, 1, 64, 64,
                                          26, 1, 1, 17, 1, 0, 1, 0,  1};

// Descriptor from an unbranded USB hub frequently used with Pixelbook -- obtained through USB
// packet capture.
const uint8_t kUnbrandedHubDescriptor[] = {9, 2, 31, 0, 1,   1,  0, 224, 0, 9, 4,  0, 0, 1, 9, 0,
                                           0, 0, 7,  5, 129, 19, 2, 0,   8, 6, 48, 0, 0, 2, 0};
const uint8_t kUnbrandedHubDescriptor2[] = {12, 42, 4, 9, 0, 100, 0, 4, 250, 0, 0, 0};
const uint8_t kUnbrandedDeviceDescriptor[] = {18, 1,  16, 2,  9,   0, 1, 64, 9,
                                              33, 19, 40, 17, 144, 1, 2, 0,  1};

// Hub to emulate
enum class EmulationMode {
  // SMAYS OTG hub
  Smays = 0,
  // Unbranded USB type C hub that is frequently used with Pixelbook.
  Unbranded = 1,
};

struct EmulationMetadata {
  uint8_t port_count = 0;
  fbl::Span<const uint8_t> device_descriptor;
  fbl::Span<const uint8_t> secondary_descriptor;
  fbl::Span<const uint8_t> descriptor;
  usb_speed_t speed;
  EmulationMode mode;
  explicit EmulationMetadata(EmulationMode mode) : mode(mode) {
    switch (mode) {
      case EmulationMode::Smays:
        descriptor = fbl::Span(kSmaysHubDescriptor, sizeof(kSmaysHubDescriptor));
        secondary_descriptor = fbl::Span(kSmaysHubDescriptor2, sizeof(kSmaysHubDescriptor2));
        device_descriptor = fbl::Span(kSmaysDeviceDescriptor, sizeof(kSmaysDeviceDescriptor));
        port_count = 4;
        speed = USB_SPEED_HIGH;
        break;
      case EmulationMode::Unbranded:
        descriptor = fbl::Span(kUnbrandedHubDescriptor, sizeof(kUnbrandedHubDescriptor));
        device_descriptor =
            fbl::Span(kUnbrandedDeviceDescriptor, sizeof(kUnbrandedDeviceDescriptor));
        secondary_descriptor =
            fbl::Span(kUnbrandedHubDescriptor2, sizeof(kUnbrandedHubDescriptor2));
        port_count = 4;
        speed = USB_SPEED_SUPER;
        break;
    }
  }
};

enum class PortStatusBit : uint8_t {
  kConnected = 0,
  kEnabled = 1,
  kSuspended = 2,
  kOvercurrent = 3,
  kReset = 4,
  kBHPortReset = 5,
  kLinkState = 6,
  kConfigError = 7,
  kPower = 8,
  kLowSpeed = 9,
  kHighSpeed = 10,
  kTestMode = 11,
  kIndicatorControl = 12,
};

class PortStatus {
 public:
  // Returns the status mask in response to a GET_PORT_STATUS request
  // and clears the change mask.
  usb_port_status_t GetStatus() {
    fbl::AutoLock l(&mutex_);
    usb_port_status_t status_value;
    status_value.w_port_change = change_mask_;
    status_value.w_port_status = status_mask_;
    return status_value;
  }

  void ClearFeature(PortStatusBit bit) {
    fbl::AutoLock l(&mutex_);
    change_mask_ &= ~(1 << static_cast<uint8_t>(bit));
  }

  void SetBit(PortStatusBit bit) {
    fbl::AutoLock l(&mutex_);
    status_mask_ |= (1 << static_cast<uint8_t>(bit));
    // HighSpeed and LowSpeed bits don't generate change notifications
    if (!((bit == PortStatusBit::kHighSpeed) || (bit == PortStatusBit::kLowSpeed) ||
          (bit == PortStatusBit::kPower))) {
      change_mask_ |= (1 << static_cast<uint8_t>(bit));
    }
  }

  bool CheckBit(PortStatusBit bit) {
    fbl::AutoLock l(&mutex_);
    return status_mask_ & (1 << static_cast<uint8_t>(bit));
  }

  void ClearBit(PortStatusBit bit) {
    fbl::AutoLock l(&mutex_);
    status_mask_ &= ~(1 << static_cast<uint8_t>(bit));
    if (!((bit == PortStatusBit::kHighSpeed) || (bit == PortStatusBit::kLowSpeed) ||
          (bit == PortStatusBit::kPower))) {
      change_mask_ |= (1 << static_cast<uint8_t>(bit));
    }
  }

 private:
  fbl::Mutex mutex_;
  uint16_t status_mask_ __TA_GUARDED(mutex_) = 0;
  uint16_t change_mask_ __TA_GUARDED(mutex_) = 0;
};

static std::unique_ptr<IOEntry> MakeSyncEntry(OperationType type) {
  return std::make_unique<IOEntry>(nullptr, type);
}

static void Complete(std::unique_ptr<IOEntry> entry) {
  if (!entry->complete_queue) {
    return;
  }
  entry->complete_queue->Insert(std::move(entry));
}

class FakeDevice;
class FakeDevice : public ddk::UsbBusProtocol<FakeDevice>, public ddk::UsbProtocol<FakeDevice> {
 public:
  explicit FakeDevice(EmulationMode mode)
      : loop_(&kAsyncLoopConfigNeverAttachToThread), emulation_(mode) {
    queue_.StartThread(fit::bind_member(this, &FakeDevice::MessageLoop));
    request_completion_.StartThread(fit::bind_member(this, &FakeDevice::CompletionThread));
    outgoing_synchronous_methods_.StartThread(
        fit::bind_member(this, &FakeDevice::SynchronousDispatchThread));
    outgoing_asynchronous_methods_.StartThread(
        fit::bind_member(this, &FakeDevice::AsyncCompletionThread));
  }

  void AsyncCompletionThread() {
    while (true) {
      auto message = outgoing_asynchronous_methods_.Wait();
      switch (message->type) {
        case OperationType::kDispatchInit:
          RunInitDispatch(message->ctx);
          Complete(std::move(message));
          break;
        case OperationType::kRelease:
          ReleaseDispatch();
          Complete(std::move(message));
          break;
        case OperationType::kUnbind:
          UnbindDispatch(message->ctx);
          Complete(std::move(message));
          break;
        case OperationType::kExitEventLoop:
          Complete(std::move(message));
          return;
        default:
          abort();
      }
    }
  }

  void SynchronousDispatchThread() {
    while (true) {
      auto message = outgoing_synchronous_methods_.Wait();
      switch (message->type) {
        case OperationType::kResetPort:
          message->status = ResetPortDispatch(static_cast<uint8_t>(message->port));
          Complete(std::move(message));
          break;
        case OperationType::kExitEventLoop:
          Complete(std::move(message));
          return;
        default:
          abort();
      }
    }
  }

  void CompletionThread() {
    while (true) {
      auto message = request_completion_.Wait();
      switch (message->type) {
        case OperationType::kUsbRequestQueue:
          message->completion.callback(message->completion.ctx, message->request);
          pending_requests_--;
          break;
        case OperationType::kExitEventLoop:
          Complete(std::move(message));
          return;
        default:
          abort();
      }
    }
  }

  void MessageLoop() {
    while (true) {
      auto message = queue_.Wait();
      switch (message->type) {
        case OperationType::kUsbRequestQueue:
          UsbRequestQueueDispatch(std::move(message));
          break;
        case OperationType::kResetPort:
          outgoing_synchronous_methods_.Insert(std::move(message));
          break;
        case OperationType::kInitCompleteEvent:
          state_change_queue_.Insert(std::move(message));
          break;
        case OperationType::kConnectDevice:
          ConnectDeviceDispatch(static_cast<uint8_t>(message->port), message->speed);
          Complete(std::move(message));
          break;
        case OperationType::kDisconnectDevice:
          DisconnectDeviceDispatch(static_cast<uint8_t>(message->port));
          Complete(std::move(message));
          break;
        case OperationType::kUsbBusDeviceAdded:
          UsbBusDeviceAddedDispatch(std::move(message));
          break;
        case OperationType::kUsbBusDeviceRemoved:
          UsbBusDeviceRemovedDispatch(std::move(message));
          break;
        case OperationType::kExitEventLoop:
          Complete(std::move(message));
          return;
        case OperationType::kHasOps:
          message->ctx = const_cast<void*>(static_cast<const void*>(ops_table_));
          Complete(std::move(message));
          break;
        case OperationType::kInterrupt:
          InterruptDispatch();
          Complete(std::move(message));
          break;
        case OperationType::kUnbindReplied:
        case OperationType::kPowerOnEvent:
          // Invalid within this context
          abort();
          break;
        case OperationType::kResetPending:
          message->port = ResetPendingDispatch(static_cast<uint8_t>(message->port));
          Complete(std::move(message));
          break;
        case OperationType::kSetOpTable:
          SetOpTableDispatch(message->ops_table, message->ctx);
          Complete(std::move(message));
          break;
        case OperationType::kUnplug:
          UnplugDispatch();
          Complete(std::move(message));
          break;
        case OperationType::kUsbBusSetHubInterface:
          UsbBusSetHubInterfaceDispatch(std::move(message));
          break;
        case OperationType::kUsbCancelAll:
          message->status = UsbCancelAllDispatch(message->ep_address);
          if (pending_requests_) {
            // TODO(fxb/60981): Make CancelAll async
            queue_.Insert(std::move(message));
          } else {
            Complete(std::move(message));
          }
          break;
        case OperationType::kUsbEnableEndpoint:
          message->status =
              UsbEnableEndpointDispatch(message->ep_desc, message->ss_com_desc, message->enable);
          Complete(std::move(message));
          break;
        case OperationType::kDispatchInit:
        case OperationType::kRelease:
        case OperationType::kUnbind:
          message->ctx = ctx_;
          outgoing_asynchronous_methods_.Insert(std::move(message));
          break;
      }
    }
  }

  void SetOpTableDispatch(const zx_protocol_device_t* ops_table, void* ctx) {
    ops_table_ = ops_table;
    ctx_ = ctx;
  }

  void UnbindDispatch(void* ctx) { ops_table_->unbind(ctx); }

  void ReleaseDispatch() {
    ops_table_->release(ctx_);
    ops_table_ = nullptr;
  }

  zx_status_t UsbBusConfigureHub(/* zx_device_t* */ uint64_t hub_device, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt) {
    if (desc->b_nbr_ports != emulation_.port_count) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (speed != emulation_.speed) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (multi_tt) {
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  }

  void UsbBusDeviceAddedDispatch(std::unique_ptr<IOEntry> entry) {
    state_change_queue_.Insert(std::move(entry));
  }

  zx_status_t UsbBusDeviceAdded(/* zx_device_t* */ uint64_t hub_device, uint32_t port,
                                usb_speed_t speed) {
    auto entry = MakeSyncEntry(OperationType::kUsbBusDeviceAdded);
    entry->hub_device = hub_device;
    entry->port = port;
    entry->speed = speed;
    return SendMessageSync(std::move(entry))->status;
  }

  void UsbBusDeviceRemovedDispatch(std::unique_ptr<IOEntry> entry) {
    state_change_queue_.Insert(std::move(entry));
  }

  zx_status_t UsbBusDeviceRemoved(/* zx_device_t* */ uint64_t hub_device, uint32_t port) {
    auto entry = MakeSyncEntry(OperationType::kUsbBusDeviceRemoved);
    entry->hub_device = hub_device;
    entry->port = port;
    return SendMessageSync(std::move(entry))->status;
  }

  void UsbBusSetHubInterfaceDispatch(std::unique_ptr<IOEntry> entry) {
    hub_protocol_ = entry->hub_interface;
    entry->status = ZX_OK;
    Complete(std::move(entry));
  }

  zx_status_t UsbBusSetHubInterface(/* zx_device_t* */ uint64_t usb_device,
                                    const usb_hub_interface_protocol_t* hub) {
    auto entry = MakeSyncEntry(OperationType::kUsbBusSetHubInterface);
    entry->hub_device = usb_device;
    entry->hub_interface = *hub;
    return SendMessageSync(std::move(entry))->status;
  }

  // USB protocol implementation
  zx_status_t UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           int64_t timeout, uint8_t* out_read_buffer, size_t read_size,
                           size_t* out_read_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                            int64_t timeout, const uint8_t* write_buffer, size_t write_size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t GeneratePortBitmask() {
    uint8_t mask = 0;
    size_t i = 1;
    for (auto& port : port_status_) {
      if (port.GetStatus().w_port_change) {
        mask |= (1 << i);
      }
      i++;
    }
    return mask;
  }

  zx_status_t ControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                         int64_t timeout, const void* write_buffer, size_t write_size) {
    enum class Opcode : uint16_t {
      SetFeature = 0x323,
      ClearFeature = 0x123,
    };
    auto request_opcode = static_cast<Opcode>(request_type | (request << 8));
    switch (request_opcode) {
      case Opcode::SetFeature: {
        switch (value) {
          case USB_FEATURE_PORT_POWER: {
            if ((index != power_on_expected_) || (index > emulation_.port_count)) {
              return ZX_ERR_INVALID_ARGS;
            }
            port_status_[index - 1].SetBit(PortStatusBit::kPower);
            power_on_expected_++;
            if (power_on_expected_ > emulation_.port_count) {
              auto msg = std::make_unique<IOEntry>(nullptr, OperationType::kPowerOnEvent);
              state_change_queue_.Insert(std::move(msg));
            }
            InterruptDispatch();
            return ZX_OK;
          } break;
          case USB_FEATURE_PORT_RESET: {
            if (port_status_[index - 1].CheckBit(PortStatusBit::kConnected)) {
              port_status_[index - 1].SetBit(PortStatusBit::kEnabled);
              port_status_[index - 1].ClearFeature(PortStatusBit::kReset);
            } else {
              port_status_[index - 1].SetBit(PortStatusBit::kReset);
            }
            InterruptDispatch();
            return ZX_OK;
          } break;
        }
      } break;
      case Opcode::ClearFeature: {
        switch (value) {
          case USB_FEATURE_C_PORT_CONNECTION:
            port_status_[index - 1].ClearFeature(PortStatusBit::kConnected);
            return ZX_OK;
          case USB_FEATURE_C_PORT_ENABLE:
            port_status_[index - 1].ClearFeature(PortStatusBit::kEnabled);
            return ZX_OK;
          case USB_FEATURE_C_PORT_SUSPEND:
            port_status_[index - 1].ClearFeature(PortStatusBit::kSuspended);
            return ZX_OK;
          case USB_FEATURE_C_PORT_OVER_CURRENT:
            port_status_[index - 1].ClearFeature(PortStatusBit::kOvercurrent);
            return ZX_OK;
          case USB_FEATURE_C_PORT_RESET:
            port_status_[index - 1].ClearFeature(PortStatusBit::kReset);
            return ZX_OK;
          case USB_C_BH_PORT_RESET:
            port_status_[index - 1].ClearFeature(PortStatusBit::kBHPortReset);
            return ZX_OK;
          case USB_FEATURE_C_PORT_LINK_STATE:
            port_status_[index - 1].ClearFeature(PortStatusBit::kLinkState);
            return ZX_OK;
          case USB_FEATURE_C_PORT_CONFIG_ERROR:
            port_status_[index - 1].ClearFeature(PortStatusBit::kConfigError);
            return ZX_OK;
        }
      } break;
    }
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t ControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                        int64_t timeout, void* out_read_buffer, size_t read_size,
                        size_t* out_read_actual) {
    enum class Opcode : uint16_t {
      GetClassDescriptor = 0x6A0,
      GetStandardDescriptor = 0x680,
      GetPortStatus = 0xA3,
    };

    auto request_opcode = static_cast<Opcode>(request_type | (request << 8));

    switch (request_opcode) {
      case Opcode::GetClassDescriptor: {
        // Type field
        switch (value >> 8) {
          case USB_HUB_DESC_TYPE_SS:
            if (emulation_.speed != USB_SPEED_SUPER) {
              break;
            }
          case USB_HUB_DESC_TYPE:
            // Fetch secondary hub descriptor
            memcpy(out_read_buffer, emulation_.secondary_descriptor.data(),
                   emulation_.secondary_descriptor.size());
            *out_read_actual = emulation_.secondary_descriptor.size();
            return ZX_OK;
        }
      } break;
      case Opcode::GetStandardDescriptor: {
        switch (value >> 8) {
          case USB_DT_DEVICE: {
            memcpy(out_read_buffer, emulation_.device_descriptor.data(),
                   emulation_.device_descriptor.size());
            *out_read_actual = emulation_.device_descriptor.size();
          }
            return ZX_OK;
        }
      } break;
      case Opcode::GetPortStatus: {
        auto status = port_status_[index - 1].GetStatus();
        memcpy(out_read_buffer, &status, sizeof(status));
        *out_read_actual = sizeof(status);
        return ZX_OK;
      } break;
    }
    return ZX_ERR_NOT_SUPPORTED;
  }
  void CompleteRequest(std::unique_ptr<IOEntry> request, zx_status_t status, zx_off_t actual) {
    request->request->response.status = status;
    request->request->response.actual = actual;
    if (synthetic_) {
      request->completion.callback(request->completion.ctx, request->request);
      pending_requests_--;
      return;
    }
    request_completion_.Insert(std::move(request));
  }
  void UsbRequestQueueDispatch(std::unique_ptr<IOEntry> entry) {
    if (request_callback_) {
      (*request_callback_)(entry->request, entry->completion);
      return;
    }
    auto usb_request = entry->request;
    if (usb_request->header.ep_address == 0) {
      // Control request
      if (usb_request->setup.bm_request_type & USB_DIR_IN) {
        void* buffer;
        usb_request_mmap(usb_request, &buffer);
        size_t size = 0;
        zx_status_t status =
            ControlIn(usb_request->setup.bm_request_type, usb_request->setup.b_request,
                      usb_request->setup.w_value, usb_request->setup.w_index, ZX_TIME_INFINITE,
                      buffer, usb_request->setup.w_length, &size);
        CompleteRequest(std::move(entry), status, size);
      } else {
        void* buffer;
        usb_request_mmap(usb_request, &buffer);
        size_t size = usb_request->setup.w_length;
        zx_status_t status =
            ControlOut(usb_request->setup.bm_request_type, usb_request->setup.b_request,
                       usb_request->setup.w_value, usb_request->setup.w_index, ZX_TIME_INFINITE,
                       buffer, usb_request->setup.w_length);
        CompleteRequest(std::move(entry), status, size);
      }
      return;
    }
    if (interrupt_pending_ || GeneratePortBitmask()) {
      interrupt_pending_ = false;
      uint8_t* mask;
      usb_request_mmap(usb_request, reinterpret_cast<void**>(&mask));
      *mask = GeneratePortBitmask();
      CompleteRequest(std::move(entry), ZX_OK, 1);
      return;
    }
    {
      if (unplugged_) {
        CompleteRequest(std::move(entry), ZX_ERR_IO_REFUSED, 0);
        return;
      }
      request_ = std::move(entry);
    }
  }

  void UsbRequestQueue(usb_request_t* request, const usb_request_complete_callback_t* completion) {
    if (synthetic_) {
      auto entry = MakeSyncEntry(OperationType::kUsbRequestQueue);
      entry->request = request;
      entry->completion = *completion;
      UsbRequestQueueDispatch(std::move(entry));
      return;
    }
    auto entry = MakeSyncEntry(OperationType::kUsbRequestQueue);
    pending_requests_++;
    entry->request = request;
    entry->completion = *completion;
    queue_.Insert(std::move(entry));
  }

  usb_speed_t UsbGetSpeed() { return emulation_.speed; }

  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t UsbGetConfiguration() { return 0; }

  zx_status_t UsbSetConfiguration(uint8_t configuration) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbEnableEndpointDispatch(const usb_endpoint_descriptor_t* ep_desc,
                                        const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                        bool enable) {
    interrupt_endpoint_ = ep_desc->b_endpoint_address;
    return ZX_OK;
  }

  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    auto entry = MakeSyncEntry(OperationType::kUsbEnableEndpoint);
    entry->ep_desc = ep_desc;
    entry->ss_com_desc = ss_com_desc;
    entry->enable = enable;
    return SendMessageSync(std::move(entry))->status;
  }

  zx_status_t UsbResetEndpoint(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbResetDevice() { return ZX_ERR_NOT_SUPPORTED; }

  size_t UsbGetMaxTransferSize(uint8_t ep_address) { return 0; }

  uint32_t UsbGetDeviceId() { return 0; }

  void UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {}

  zx_status_t UsbGetConfigurationDescriptorLength(uint8_t configuration, size_t* out_length) {
    return 0;
  }

  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, uint8_t* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  size_t UsbGetDescriptorsLength() { return emulation_.descriptor.size(); }

  void UsbGetDescriptors(uint8_t* out_descs_buffer, size_t descs_size, size_t* out_descs_actual) {
    memcpy(out_descs_buffer, emulation_.descriptor.data(), emulation_.descriptor.size());
    *out_descs_actual = emulation_.descriptor.size();
  }

  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     uint8_t* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbCancelAllDispatch(uint8_t ep_address) {
    if (request_ && ep_address) {
      auto req = std::move(request_);
      usb_request_complete(req->request, ZX_ERR_CANCELED, 0, &req->completion);
      pending_requests_--;
    }
    return ZX_OK;
  }

  zx_status_t UsbCancelAll(uint8_t ep_address) {
    auto entry = MakeSyncEntry(OperationType::kUsbCancelAll);
    entry->ep_address = ep_address;
    return SendMessageSync(std::move(entry))->status;
  }

  uint64_t UsbGetCurrentFrame() { return 0; }

  size_t UsbGetRequestSize() { return sizeof(usb_request_t); }

  usb_hub::UsbHubDevice* device() { return static_cast<usb_hub::UsbHubDevice*>(ctx_); }

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

  void ConnectDeviceDispatch(uint8_t port, usb_speed_t speed) {
    // We use zero-based indexing for ports, USB uses 1-based indexing.
    if (speed == USB_SPEED_HIGH) {
      port_status_[port].SetBit(PortStatusBit::kHighSpeed);
    }
    if (speed == USB_SPEED_LOW) {
      port_status_[port].SetBit(PortStatusBit::kLowSpeed);
    }
    port_status_[port].SetBit(PortStatusBit::kConnected);
  }

  void DisconnectDeviceDispatch(uint8_t port) {
    port_status_[port].ClearBit(PortStatusBit::kConnected);
    port_status_[port].ClearBit(PortStatusBit::kEnabled);
    port_status_[port].ClearBit(PortStatusBit::kReset);
  }

  zx_status_t ResetPortDispatch(uint8_t port) {
    return hub_protocol_.ops->reset_port(hub_protocol_.ctx, port + 1);
  }

  bool ResetPendingDispatch(uint8_t port) {
    return port_status_[port].CheckBit(PortStatusBit::kReset);
  }

  void InterruptDispatch() {
    if (request_) {
      uint8_t* mask;
      auto req = request_->request;
      usb_request_mmap(req, reinterpret_cast<void**>(&mask));
      *mask = GeneratePortBitmask();
      CompleteRequest(std::move(request_), ZX_OK, 1);
    } else {
      interrupt_pending_ = true;
    }
  }

  void UnplugDispatch() {
    if (request_) {
      CompleteRequest(std::move(request_), ZX_ERR_IO_REFUSED, 0);
    }
  }

  void SetSynthetic(bool synthetic) { synthetic_ = synthetic; }

  bool IsSynthetic() const { return synthetic_; }

  void RunInitDispatch(void* ctx) { ops_table_->init(ctx); }

  void SendMessage(std::unique_ptr<IOEntry> entry) { queue_.Insert(std::move(entry)); }

  std::unique_ptr<IOEntry> SendMessageSync(std::unique_ptr<IOEntry> entry) {
    IOQueue sync_queue;
    entry->complete_queue = &sync_queue;
    queue_.Insert(std::move(entry));
    return sync_queue.Wait();
  }

  void Release() { SendMessageSync(MakeSyncEntry(OperationType::kRelease)); }

  bool HasOps() {
    auto entry = SendMessageSync(MakeSyncEntry(OperationType::kHasOps));
    return entry->ctx;
  }

  void ConnectDevice(uint8_t port, usb_speed_t speed) {
    auto message = MakeSyncEntry(OperationType::kConnectDevice);
    message->port = port;
    message->speed = speed;
    SendMessageSync(std::move(message));
  }

  void Interrupt() { SendMessageSync(MakeSyncEntry(OperationType::kInterrupt)); }

  void Unplug() { SendMessageSync(MakeSyncEntry(OperationType::kUnplug)); }

  void Unbind() {
    SendMessageSync(MakeSyncEntry(OperationType::kUnbind));
    ZX_ASSERT(state_change_queue_.Wait()->type == OperationType::kUnbindReplied);
  }

  void SetOpTable(const zx_protocol_device_t* ops_table, void* ctx) {
    auto entry = MakeSyncEntry(OperationType::kSetOpTable);
    entry->ctx = ctx;
    entry->ops_table = ops_table;
    SendMessageSync(std::move(entry));
  }

  zx_status_t ResetPort(uint8_t port) {
    auto message = MakeSyncEntry(OperationType::kResetPort);
    message->port = port;
    message = SendMessageSync(std::move(message));
    return message->status;
  }

  bool ResetPending(uint8_t port) {
    auto message = MakeSyncEntry(OperationType::kResetPending);
    message->port = port;
    message = SendMessageSync(std::move(message));
    return message->port;
  }

  void InitComplete() { queue_.Insert(MakeSyncEntry(OperationType::kInitCompleteEvent)); }

  void NotifyRemoved() {
    auto entry = MakeSyncEntry(OperationType::kUnbindReplied);
    state_change_queue_.Insert(std::move(entry));
  }

  void RunInit() {
    auto message = MakeSyncEntry(OperationType::kDispatchInit);
    SendMessageSync(std::move(message));
  }

  void DisconnectDevice(uint8_t port) {
    auto message = MakeSyncEntry(OperationType::kDisconnectDevice);
    message->port = port;
    SendMessageSync(std::move(message));
  }

  IOQueue& GetStateChangeQueue() { return state_change_queue_; }

  void SetRequestCallback(
      fit::function<void(usb_request_t*, usb_request_complete_callback_t)> callback) {
    ZX_ASSERT(synthetic_);
    request_callback_ = std::move(callback);
  }

 private:
  std::atomic_size_t pending_requests_ = 0;
  // State change queue which is read from by a test
  IOQueue state_change_queue_;
  // Incoming request queue
  IOQueue queue_;
  // Queue for outgoing synchronous method invocations
  IOQueue outgoing_synchronous_methods_;
  // Queue for outgoing calls to async methods.
  IOQueue outgoing_asynchronous_methods_;
  // Request completion queue
  IOQueue request_completion_;
  // Indicates whether or not this test is synthetic.
  // Thread-safety: Must only be written during object construction.
  // May be read safely from any thread.
  bool synthetic_ = false;
  // Control requests pending count
  // Thread-safety: May be safely read or written from any thread provided that
  // accesses are performed atomically.
  std::atomic_int32_t control_requests_pending_ = 0;
  // Completion event that indicates all control requests have been cleared during shutdown
  sync_completion_t control_request_cleared_;
  // Indicates whether or not a simulated interrupt is pending (simulated hardware register).
  bool interrupt_pending_ = false;
  async::Loop loop_;
  // Event waiting for an unbind to complete
  bool unplugged_ = false;
  std::unique_ptr<IOEntry> request_;
  fit::function<zx_status_t(uint32_t, usb_speed_t)> connect_callback_;
  // Whether or not power on is expected (simulated hardware register)
  uint8_t power_on_expected_ = 1;
  // Port status (simulated hardware register)
  PortStatus port_status_[7];
  // Interrupt endpoint set by UsbEnableEndpoint
  uint8_t interrupt_endpoint_ = 0;
  const zx_protocol_device_t* ops_table_ = nullptr;
  void* ctx_;
  EmulationMetadata emulation_;
  usb_hub_interface_protocol_t hub_protocol_;
  std::optional<fit::function<void(usb_request_t*, usb_request_complete_callback_t)>>
      request_callback_;
};

#endif  // SRC_DEVICES_USB_DRIVERS_USB_HUB_REWRITE_FAKE_DEVICE_H_
