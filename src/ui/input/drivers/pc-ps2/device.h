// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_PC_PS2_DEVICE_H_
#define SRC_UI_INPUT_DRIVERS_PC_PS2_DEVICE_H_

#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/zx/interrupt.h>

#include <condition_variable>

#include <ddktl/device.h>
#include <ddktl/unbind-txn.h>
#include <hid/boot.h>

#include "src/ui/input/drivers/pc-ps2/controller.h"

namespace i8042 {
enum ModStatus {
  kSet = 1,
  kExists = 2,
  kRollover = 3,
};

enum KeyStatus {
  kKeyAdded = 1,
  kKeyExists = 2,
  kKeyRollover = 3,
  kKeyRemoved = 4,
  kKeyNotFound = 5,
};
constexpr uint8_t kAck = 0xfa;

class I8042Device;
using DeviceType = ddk::Device<I8042Device, ddk::Unbindable>;
class I8042Device : public DeviceType, public ddk::HidbusProtocol<I8042Device, ddk::base_protocol> {
 public:
  explicit I8042Device(Controller* parent, Port port)
      : DeviceType(parent->zxdev()), controller_(parent), port_(port) {
    memset(&reports_, 0, sizeof(reports_));
  }

  static zx_status_t Bind(Controller* parent, Port port);
  zx_status_t Bind();

  void DdkRelease() {
    irq_thread_.join();
    delete this;
  }
  void DdkUnbind(ddk::UnbindTxn txn);

  // Hid bus ops
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* out_info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, uint8_t* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                              size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_OK; }
  zx_status_t HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

 private:
  Controller* controller_;
  Port port_;
  fuchsia_hardware_input::wire::BootProtocol protocol_;
  std::thread irq_thread_;
  zx::interrupt irq_;
  std::mutex unbind_lock_;
  std::condition_variable_any unbind_ready_;
  std::optional<ddk::UnbindTxn> unbind_ __TA_GUARDED(unbind_lock_);

  std::mutex hid_lock_;
  ddk::HidbusIfcProtocolClient ifc_ __TA_GUARDED(hid_lock_);

  uint8_t last_code_ = 0;
  union {
    hid_boot_kbd_report_t keyboard_report;
    hid_boot_mouse_report_t mouse_report;
  } reports_;
  hid_boot_kbd_report_t& keyboard_report() { return reports_.keyboard_report; }
  hid_boot_mouse_report_t& mouse_report() { return reports_.mouse_report; }

  zx::status<fuchsia_hardware_input::wire::BootProtocol> Identify();
  void IrqThread();
  // Keyboard input
  void ProcessScancode(uint8_t code);
  ModStatus ModifierKey(uint8_t usage, bool down);
  KeyStatus AddKey(uint8_t usage);
  KeyStatus RemoveKey(uint8_t usage);
  // Mouse input
  void ProcessMouse(uint8_t code);
};

}  // namespace i8042

#endif  // SRC_UI_INPUT_DRIVERS_PC_PS2_DEVICE_H_
