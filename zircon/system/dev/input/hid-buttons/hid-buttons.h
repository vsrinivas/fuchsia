// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_INPUT_HID_BUTTONS_HID_BUTTONS_H_
#define ZIRCON_SYSTEM_DEV_INPUT_HID_BUTTONS_HID_BUTTONS_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>

#include <map>
#include <optional>
#include <vector>

#include <ddk/metadata/buttons.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/device.h>
#include <ddktl/protocol/buttons.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <hid/buttons.h>

#include "ddk/protocol/buttons.h"

// == operator for button_notify_callback_t
inline bool operator==(const button_notify_callback_t& c1, const button_notify_callback_t c2) {
  return (c1.notify_button == c2.notify_button) && (c1.ctx == c2.ctx);
}

namespace buttons {

// zx_port_packet::key.
constexpr uint64_t kPortKeyShutDown = 0x01;
// Start of up to kNumberOfRequiredGpios port types used for interrupts.
constexpr uint64_t kPortKeyInterruptStart = 0x10;

class HidButtonsDevice;
using DeviceType = ddk::Device<HidButtonsDevice, ddk::UnbindableDeprecated>;
class HidButtonsHidBusFunction;
using HidBusFunctionType = ddk::Device<HidButtonsHidBusFunction, ddk::UnbindableDeprecated>;
class HidButtonsButtonsFunction;
using ButtonsFunctionType = ddk::Device<HidButtonsButtonsFunction, ddk::UnbindableDeprecated>;

class HidButtonsDevice : public DeviceType {
 public:
  struct Gpio {
    gpio_protocol_t gpio;
    zx::interrupt irq;
    buttons_gpio_config_t config;
  };

  explicit HidButtonsDevice(zx_device_t* device) : DeviceType(device) {}
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
  bool ButtonsGetState(button_type_t type);
  zx_status_t ButtonsRegisterNotifyButton(button_type_t type,
                                          const button_notify_callback_t* callback);
  void ButtonsUnregisterNotifyButton(button_type_t type, const button_notify_callback_t* callback);

  void DdkUnbindDeprecated();
  void DdkRelease();

  zx_status_t Bind(fbl::Array<Gpio> gpios, fbl::Array<buttons_button_config_t> buttons);

 protected:
  // Protected for unit testing.
  void ShutDown() TA_EXCL(client_lock_);

  zx::port port_;

  fbl::Mutex callbacks_lock_;
  fbl::Array<std::vector<button_notify_callback_t>> callbacks_ TA_GUARDED(callbacks_lock_);
  // only for DIRECT; callbacks_, gpios_ and buttons_ are 1:1:1 in the same order
  std::map<uint8_t, uint32_t> button_map_;  // Button ID to Button Number

 private:
  HidButtonsHidBusFunction* hidbus_function_;
  HidButtonsButtonsFunction* buttons_function_;

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
};

class HidButtonsHidBusFunction
    : public HidBusFunctionType,
      public ddk::HidbusProtocol<HidButtonsHidBusFunction, ddk::base_protocol>,
      public fbl::RefCounted<HidButtonsHidBusFunction> {
 public:
  explicit HidButtonsHidBusFunction(zx_device_t* device, HidButtonsDevice* peripheral)
      : HidBusFunctionType(device), peripheral_(peripheral) {}
  virtual ~HidButtonsHidBusFunction() = default;

  void DdkUnbindDeprecated() { DdkRemoveDeprecated(); }
  void DdkRelease() { delete this; }

  // Methods required by the ddk mixins.
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) {
    return peripheral_->HidbusStart(ifc);
  }
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info) {
    return peripheral_->HidbusQuery(options, info);
  }
  void HidbusStop() { peripheral_->HidbusStop(); }
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual) {
    return peripheral_->HidbusGetDescriptor(desc_type, out_data_buffer, data_size, out_data_actual);
  }
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len) {
    return peripheral_->HidbusGetReport(rpt_type, rpt_id, data, len, out_len);
  }
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len) {
    return peripheral_->HidbusSetReport(rpt_type, rpt_id, data, len);
  }
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return peripheral_->HidbusGetIdle(rpt_id, duration);
  }
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return peripheral_->HidbusSetIdle(rpt_id, duration);
  }
  zx_status_t HidbusGetProtocol(uint8_t* protocol) {
    return peripheral_->HidbusGetProtocol(protocol);
  }
  zx_status_t HidbusSetProtocol(uint8_t protocol) {
    return peripheral_->HidbusSetProtocol(protocol);
  }

 private:
  HidButtonsDevice* peripheral_;
};

class HidButtonsButtonsFunction
    : public ButtonsFunctionType,
      public ddk::ButtonsProtocol<HidButtonsButtonsFunction, ddk::base_protocol>,
      public fbl::RefCounted<HidButtonsButtonsFunction> {
 public:
  explicit HidButtonsButtonsFunction(zx_device_t* device, HidButtonsDevice* peripheral)
      : ButtonsFunctionType(device), peripheral_(peripheral) {}
  virtual ~HidButtonsButtonsFunction() = default;

  void DdkUnbindDeprecated() { DdkRemoveDeprecated(); }
  void DdkRelease() { delete this; }

  // Methods required by the ddk mixins.
  bool ButtonsGetState(button_type_t type) { return peripheral_->ButtonsGetState(type); }
  zx_status_t ButtonsRegisterNotifyButton(button_type_t type,
                                          const button_notify_callback_t* callback) {
    return peripheral_->ButtonsRegisterNotifyButton(type, callback);
  }
  void ButtonsUnregisterNotifyButton(button_type_t type, const button_notify_callback_t* callback) {
    peripheral_->ButtonsUnregisterNotifyButton(type, callback);
  }

 private:
  HidButtonsDevice* peripheral_;
};

}  // namespace buttons

#endif  // ZIRCON_SYSTEM_DEV_INPUT_HID_BUTTONS_HID_BUTTONS_H_
