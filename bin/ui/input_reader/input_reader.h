// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_INPUT_READER_H_
#define GARNET_BIN_UI_INPUT_READER_INPUT_READER_H_

#include <map>
#include <utility>

#include <async/cpp/auto_wait.h>
#include "garnet/bin/ui/input_reader/input_interpreter.h"
#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"
#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ui/input/fidl/input_reports.fidl.h"

namespace mozart {
namespace input {

struct DeviceInfo;

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
  InputReader(mozart::InputDeviceRegistry* registry,
              bool ignore_console = false);
  ~InputReader();

  void Start();

 private:

  void WatchDisplayOwnershipChanges(int dir_fd);

  void DeviceAdded(std::unique_ptr<InputInterpreter> interpreter);
  void DeviceRemoved(zx_handle_t handle);

  async_wait_result_t OnDeviceHandleReady(zx_handle_t handle,
                                          zx_status_t status,
                                          const zx_packet_signal_t* signal);
  async_wait_result_t OnDisplayHandleReady(async_t* async,
                                           zx_status_t status,
                                           const zx_packet_signal_t* signal);

  mozart::InputDeviceRegistry* const registry_;
  const bool ignore_console_;

  std::map<zx_handle_t, std::unique_ptr<DeviceInfo>> devices_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;
  std::unique_ptr<fsl::DeviceWatcher> console_watcher_;
  zx_handle_t display_ownership_event_;
  std::unique_ptr<async::AutoWait> display_ownership_waiter_;
  bool display_owned_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};

}  // namespace input
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_READER_H_
