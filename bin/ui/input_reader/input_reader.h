// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_INPUT_READER_H_
#define GARNET_BIN_UI_INPUT_READER_INPUT_READER_H_

#include <map>
#include <utility>

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include "garnet/bin/ui/input_reader/input_interpreter.h"
#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"

namespace mozart {

// InputReader does four things:
// 1- Watches who owns the display, which can be us, or the console.
// 2- Watches new devices that are added to dev/class/input and then
//    create an InputInterpreter for each one.
// 3- When the device is ready for read call InputInterpreter::Read()
// 4- When devices are removed, undo #2 and #3.
//
// |ignore_console| in the ctor indicates that the reader will
// process device input even if the console owns the display.
class InputReader {
 public:
  InputReader(fuchsia::ui::input::InputDeviceRegistry* registry,
              bool ignore_console = false);
  ~InputReader();

  void Start();
  void SetOwnershipEvent(zx::event event);

 private:
  struct DeviceInfo;

  void WatchDisplayOwnershipChanges(int dir_fd);

  void DeviceAdded(std::unique_ptr<InputInterpreter> interpreter);
  void DeviceRemoved(zx_handle_t handle);

  void OnDeviceHandleReady(async_dispatcher_t* dispatcher,
                           async::WaitBase* wait, zx_status_t status,
                           const zx_packet_signal_t* signal);
  void OnDisplayHandleReady(async_dispatcher_t* dispatcher,
                            async::WaitBase* wait, zx_status_t status,
                            const zx_packet_signal_t* signal);

  fuchsia::ui::input::InputDeviceRegistry* const registry_;
  const bool ignore_console_;

  std::map<zx_handle_t, std::unique_ptr<DeviceInfo>> devices_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;
  std::unique_ptr<fsl::DeviceWatcher> console_watcher_;
  zx_handle_t display_ownership_event_;
  async::WaitMethod<InputReader, &InputReader::OnDisplayHandleReady>
      display_ownership_waiter_{this};
  bool display_owned_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_READER_H_
