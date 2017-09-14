// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_READER_INPUT_READER_H_
#define APPS_MOZART_SRC_INPUT_READER_INPUT_READER_H_

#include <map>
#include <utility>

#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ui/input/fidl/input_reports.fidl.h"
#include "garnet/bin/ui/input_reader/input_interpreter.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/io/device_watcher.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace mozart {
namespace input {

class InputReader : fsl::MessageLoopHandler {
 public:
  InputReader(mozart::InputDeviceRegistry* registry,
              bool ignore_console = false);
  ~InputReader();
  void Start();

 private:
  class DeviceInfo {
   public:
    DeviceInfo(std::unique_ptr<InputInterpreter> interpreter,
               fsl::MessageLoop::HandlerKey key);
    ~DeviceInfo();

    InputInterpreter* interpreter() { return interpreter_.get(); }
    fsl::MessageLoop::HandlerKey key() { return key_; };

   private:
    std::unique_ptr<InputInterpreter> interpreter_;
    fsl::MessageLoop::HandlerKey key_;

    FXL_DISALLOW_COPY_AND_ASSIGN(DeviceInfo);
  };

  void WatchDisplayOwnershipChanges(int dir_fd);

  void DeviceAdded(std::unique_ptr<InputInterpreter> interpreter);
  void DeviceRemoved(zx_handle_t handle);

  void OnDirectoryHandleReady(zx_handle_t handle, zx_signals_t pending);
  void OnDeviceHandleReady(zx_handle_t handle, zx_signals_t pending);
  void OnDisplayHandleReady(zx_handle_t handle, zx_signals_t pending);

  void OnInternalReport(zx_handle_t handle, InputInterpreter::ReportType type);

  // |fsl::MessageLoopHandler|:
  void OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count);

  mozart::InputDeviceRegistry* registry_;

  std::map<zx_handle_t, std::unique_ptr<DeviceInfo>> devices_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;
  std::unique_ptr<fsl::DeviceWatcher> console_watcher_;
  zx_handle_t display_ownership_event_;
  fsl::MessageLoop::HandlerKey display_ownership_handler_key_;
  bool ignore_console_;
  bool display_owned_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_INPUT_READER_INPUT_READER_H_
