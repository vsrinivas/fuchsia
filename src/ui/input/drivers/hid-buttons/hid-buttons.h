// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_BUTTONS_HID_BUTTONS_H_
#define SRC_UI_INPUT_DRIVERS_HID_BUTTONS_HID_BUTTONS_H_

#include <fuchsia/buttons/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <lib/zx/timer.h>

#include <list>
#include <map>
#include <optional>
#include <vector>

#include <ddk/metadata/buttons.h>
#include <ddk/protocol/buttons.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/buttons.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <hid/buttons.h>

#include "lib/zx/channel.h"
#include "zircon/types.h"

namespace buttons {

// zx_port_packet::key.
constexpr uint64_t kPortKeyShutDown = 0x01;
// Start of up to kNumberOfRequiredGpios port types used for interrupts.
constexpr uint64_t kPortKeyInterruptStart = 0x10;
// Timer start
constexpr uint64_t kPortKeyTimerStart = 0x100;
// Debounce threshold.
constexpr uint64_t kDebounceThresholdNs = 50'000'000;

class HidButtonsDevice;
using DeviceType = ddk::Device<HidButtonsDevice, ddk::Unbindable>;
class HidButtonsHidBusFunction;
using HidBusFunctionType = ddk::Device<HidButtonsHidBusFunction, ddk::Unbindable>;
class HidButtonsButtonsFunction;
using ButtonsFunctionType = ddk::Device<HidButtonsButtonsFunction, ddk::Unbindable>;
class ButtonsNotifyInterface;

using Buttons = ::llcpp::fuchsia::buttons::Buttons;
using ButtonType = ::llcpp::fuchsia::buttons::ButtonType;

class HidButtonsDevice : public DeviceType {
 public:
  struct Gpio {
    gpio_protocol_t gpio;
    zx::interrupt irq;
    buttons_gpio_config_t config;
  };

  explicit HidButtonsDevice(zx_device_t* device)
      : DeviceType(device), button2channels_(static_cast<size_t>(ButtonType::MAX)) {}
  virtual ~HidButtonsDevice() = default;

  // Hidbus Protocol Functions.
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) TA_EXCL(client_lock_);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  void HidbusStop() TA_EXCL(client_lock_);
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len) TA_EXCL(client_lock_);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  // Buttons Protocol Functions.
  zx_status_t ButtonsGetChannel(zx::channel chan, async_dispatcher_t* dispatcher);

  // FIDL Interface Functions.
  bool GetState(ButtonType type);
  zx_status_t RegisterNotify(uint8_t types, uint64_t chan_id);

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t Bind(fbl::Array<Gpio> gpios, fbl::Array<buttons_button_config_t> buttons);
  virtual void ClosingChannel(uint64_t id);
  virtual void Notify(uint32_t type);

 protected:
  // Protected for unit testing.
  void ShutDown() TA_EXCL(client_lock_);
  HidButtonsButtonsFunction* GetButtonsFunction() { return buttons_function_; }

  zx::port port_;

  fbl::Mutex channels_lock_;
  // only for DIRECT; interfaces_, gpios_ and buttons_ are 1:1:1 in the same order
  // button2channels_ stores the IDs of the channels, where the IDs are equivalent to the
  //    addresses/pointers to the ButtonsNotifyInterface struct containing the unowned channel.
  //    the ID allows us to identify the unowned channel so we can remove it from this struct
  //    when the corresponding channel is closed.
  std::vector<std::vector<ButtonsNotifyInterface*>> button2channels_ TA_GUARDED(channels_lock_);
  std::list<ButtonsNotifyInterface> interfaces_ TA_GUARDED(channels_lock_);  // owns the channels
  std::map<uint8_t, uint32_t> button_map_;  // Button ID to Button Number

  HidButtonsHidBusFunction* hidbus_function_;
  HidButtonsButtonsFunction* buttons_function_;

 private:
  friend class HidButtonsDeviceTest;

  int Thread();
  uint8_t ReconfigurePolarity(uint32_t idx, uint64_t int_port);
  zx_status_t ConfigureInterrupt(uint32_t idx, uint64_t int_port);
  bool MatrixScan(uint32_t row, uint32_t col, zx_duration_t delay);

  thrd_t thread_;
  fbl::Mutex client_lock_;
  ddk::HidbusIfcProtocolClient client_ TA_GUARDED(client_lock_);
  fbl::Array<buttons_button_config_t> buttons_;
  fbl::Array<Gpio> gpios_;
  std::optional<uint8_t> fdr_gpio_;

  struct debounce_state {
    bool enqueued;
    zx::timer timer;
    bool value;
  };
  fbl::Array<debounce_state> debounce_states_;
  // last_report_ saved to de-duplicate reports
  buttons_input_rpt_t last_report_;
};

class HidButtonsHidBusFunction
    : public HidBusFunctionType,
      public ddk::HidbusProtocol<HidButtonsHidBusFunction, ddk::base_protocol>,
      public fbl::RefCounted<HidButtonsHidBusFunction> {
 public:
  explicit HidButtonsHidBusFunction(zx_device_t* device, HidButtonsDevice* peripheral)
      : HidBusFunctionType(device), device_(peripheral) {}
  virtual ~HidButtonsHidBusFunction() = default;

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // Methods required by the ddk mixins.
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) { return device_->HidbusStart(ifc); }
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info) {
    return device_->HidbusQuery(options, info);
  }
  void HidbusStop() { device_->HidbusStop(); }
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual) {
    return device_->HidbusGetDescriptor(desc_type, out_data_buffer, data_size, out_data_actual);
  }
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len) {
    return device_->HidbusGetReport(rpt_type, rpt_id, data, len, out_len);
  }
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len) {
    return device_->HidbusSetReport(rpt_type, rpt_id, data, len);
  }
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return device_->HidbusGetIdle(rpt_id, duration);
  }
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return device_->HidbusSetIdle(rpt_id, duration);
  }
  zx_status_t HidbusGetProtocol(uint8_t* protocol) { return device_->HidbusGetProtocol(protocol); }
  zx_status_t HidbusSetProtocol(uint8_t protocol) { return device_->HidbusSetProtocol(protocol); }

 private:
  HidButtonsDevice* device_;
};

class HidButtonsButtonsFunction
    : public ButtonsFunctionType,
      public ddk::ButtonsProtocol<HidButtonsButtonsFunction, ddk::base_protocol>,
      public fbl::RefCounted<HidButtonsButtonsFunction> {
 public:
  HidButtonsButtonsFunction(zx_device_t* device, HidButtonsDevice* peripheral)
      : ButtonsFunctionType(device),
        device_(peripheral),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("hid-buttons-notify-loop", &loop_thread_);
  }
  virtual ~HidButtonsButtonsFunction() = default;

  void DdkUnbind(ddk::UnbindTxn txn) {
    loop_.Shutdown();
    txn.Reply();
  }
  void DdkRelease() { delete this; }

  // Methods required by the ddk mixins.
  zx_status_t ButtonsGetChannel(zx::channel chan) {
    return device_->ButtonsGetChannel(std::move(chan), loop_.dispatcher());
  }

 private:
  HidButtonsDevice* device_;

  async::Loop loop_;
  thrd_t loop_thread_;
};

class ButtonsNotifyInterface : public Buttons::Interface {
 public:
  explicit ButtonsNotifyInterface(HidButtonsDevice* peripheral) : device_(peripheral) {}
  ~ButtonsNotifyInterface() = default;

  zx_status_t Init(async_dispatcher_t* dispatcher, zx::channel chan, uint64_t id) {
    id_ = id;

    fidl::OnUnboundFn<ButtonsNotifyInterface> unbound =
        [this](ButtonsNotifyInterface*, fidl::UnbindInfo, zx::channel) {
          device_->ClosingChannel(id_);
        };
    auto res = fidl::BindServer(dispatcher, std::move(chan), this, std::move(unbound));
    if (res.is_error())
      return res.error();
    binding_ = res.take_value();
    return ZX_OK;
  }

  uint64_t id() const { return id_; }
  const fidl::ServerBindingRef<Buttons>& binding() { return *binding_; }

  // Methods required by the FIDL interface
  void GetState(ButtonType type, GetStateCompleter::Sync _completer) {
    _completer.Reply(device_->GetState(type));
  }
  void RegisterNotify(uint8_t types, RegisterNotifyCompleter::Sync _completer) {
    zx_status_t status = ZX_OK;
    if ((status = device_->RegisterNotify(types, id_)) == ZX_OK) {
      _completer.ReplySuccess();
    } else {
      _completer.ReplyError(status);
    }
  }

 private:
  HidButtonsDevice* device_;
  uint64_t id_;
  std::optional<fidl::ServerBindingRef<Buttons>> binding_;
};

}  // namespace buttons

#endif  // SRC_UI_INPUT_DRIVERS_HID_BUTTONS_HID_BUTTONS_H_
