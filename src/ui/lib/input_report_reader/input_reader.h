// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_REPORT_READER_INPUT_READER_H_
#define SRC_UI_LIB_INPUT_REPORT_READER_INPUT_READER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

#include <map>

#include "src/lib/fxl/macros.h"
#include "src/ui/lib/input_report_reader/device_watcher.h"

namespace ui_input {

class InputInterpreter;
class InputReaderBase {
 public:
  virtual bool ActiveInput() = 0;
  virtual void RemoveDevice(InputInterpreter* device) = 0;
};

// InputReader does four things:
// 1- Watches who owns the display, which can be us, or the console.
// 2- Watches new devices that are added to dev/class/input-report and then
//    create an InputInterpreter for each one.
// 3- When the device is ready for read call InputInterpreter::Read()
// 4- When devices are removed, undo #2 and #3.
//
// |ignore_console| in the ctor indicates that the reader will
// process device input even if the console owns the display.
class InputReader : public InputReaderBase {
 public:
  InputReader(fuchsia::ui::input::InputDeviceRegistry* registry, bool ignore_console = false);
  virtual ~InputReader();

  // Starts the |InputReader| with the default FDIO device watcher.
  void Start();
  // Starts the |InputReader| with a custom device watcher (e.g. for testing).
  void Start(std::unique_ptr<DeviceWatcher> device_watcher);
  void SetOwnershipEvent(zx::event event);

  bool ActiveInput() override { return ignore_console_ || display_owned_; }

  void RemoveDevice(InputInterpreter* device) override;

 private:
  void WatchDisplayOwnershipChanges(int dir_fd);

  void OnDisplayHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status, const zx_packet_signal_t* signal);

  fuchsia::ui::input::InputDeviceRegistry* const registry_;
  const bool ignore_console_;
  size_t next_interpreter_id_ = 0;

  std::map<InputInterpreter*, std::unique_ptr<InputInterpreter>> devices_;
  std::unique_ptr<DeviceWatcher> device_watcher_;
  zx::event display_ownership_event_;
  async::WaitMethod<InputReader, &InputReader::OnDisplayHandleReady> display_ownership_waiter_{
      this};
  bool display_owned_ = true;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(InputReader);
};

}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_REPORT_READER_INPUT_READER_H_
