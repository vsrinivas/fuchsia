// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_PC_PS2_DEVICE_H_
#define SRC_UI_INPUT_DRIVERS_PC_PS2_DEVICE_H_

#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/input_report_reader/reader.h>
#include <lib/zx/interrupt.h>

#include <condition_variable>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/unbind-txn.h>
#include <hid/boot.h>

#include "src/ui/input/drivers/pc-ps2/controller.h"

namespace i8042 {

struct PS2KbdInputReport {
  size_t num_pressed_keys_3 = 0;
  fuchsia_input::wire::Key pressed_keys_3[fuchsia_input_report::wire::kKeyboardMaxNumKeys];

  void Reset() { num_pressed_keys_3 = 0; }
};

struct PS2MouseInputReport {
  uint8_t buttons;
  int8_t rel_x;
  int8_t rel_y;

  void Reset() {
    buttons = 0;
    rel_x = 0;
    rel_y = 0;
  }
};

struct PS2InputReport {
  zx::time event_time;
  fuchsia_hardware_input::BootProtocol type;
  std::variant<PS2KbdInputReport, PS2MouseInputReport> report;

  void ToFidlInputReport(
      fidl::WireTableBuilder<::fuchsia_input_report::wire::InputReport>& input_report,
      fidl::AnyArena& allocator);
  void Reset() {
    event_time = {};
    type = fuchsia_hardware_input::BootProtocol::kNone;
    if (std::holds_alternative<PS2KbdInputReport>(report)) {
      std::get<PS2KbdInputReport>(report).Reset();
    } else if (std::holds_alternative<PS2MouseInputReport>(report)) {
      std::get<PS2MouseInputReport>(report).Reset();
    }
  }
};

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
using DeviceType = ddk::Device<I8042Device, ddk::Unbindable,
                               ddk::Messageable<fuchsia_input_report::InputDevice>::Mixin>;
class I8042Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  explicit I8042Device(Controller* parent, Port port)
      : DeviceType(parent->zxdev()),
        controller_(parent),
        port_(port),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        report_({
            .event_time = {},
            .type = fuchsia_hardware_input::BootProtocol::kNone,
        }) {}

  static zx_status_t Bind(Controller* parent, Port port);
  zx_status_t Bind();

  void DdkRelease() {
    irq_thread_.join();
    delete this;
  }
  void DdkUnbind(ddk::UnbindTxn txn);

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(SendOutputReportRequestView request,
                        SendOutputReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void GetFeatureReport(GetFeatureReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void SetFeatureReport(SetFeatureReportRequestView request,
                        SetFeatureReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void GetInputReport(GetInputReportRequestView request,
                      GetInputReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

#ifdef PS2_TEST
  zx_status_t WaitForNextReader(zx::duration timeout) {
    zx_status_t status = sync_completion_wait(&next_reader_wait_, timeout.get());
    if (status == ZX_OK) {
      sync_completion_reset(&next_reader_wait_);
    }
    return status;
  }
#endif

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
  input_report_reader::InputReportReaderManager<PS2InputReport> input_report_readers_
      __TA_GUARDED(hid_lock_);
#ifdef PS2_TEST
  sync_completion_t next_reader_wait_;
#endif
  async::Loop loop_;

  uint8_t last_code_ = 0;
  PS2InputReport report_;
  PS2KbdInputReport& keyboard_report() {
    ZX_ASSERT(std::holds_alternative<PS2KbdInputReport>(report_.report));
    return std::get<PS2KbdInputReport>(report_.report);
  }
  PS2MouseInputReport& mouse_report() {
    ZX_ASSERT(std::holds_alternative<PS2MouseInputReport>(report_.report));
    return std::get<PS2MouseInputReport>(report_.report);
  }

  zx::result<fuchsia_hardware_input::wire::BootProtocol> Identify();
  void IrqThread();
  // Keyboard input
  void ProcessScancode(zx::time timestamp, uint8_t code);
  KeyStatus AddKey(fuchsia_input::wire::Key key);
  KeyStatus RemoveKey(fuchsia_input::wire::Key key);
  // Mouse input
  void ProcessMouse(zx::time timestamp, uint8_t code);
};

}  // namespace i8042

#endif  // SRC_UI_INPUT_DRIVERS_PC_PS2_DEVICE_H_
