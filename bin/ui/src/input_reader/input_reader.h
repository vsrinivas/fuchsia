// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_READER_INPUT_READER_H_
#define APPS_MOZART_SRC_INPUT_READER_INPUT_READER_H_

#include <map>
#include <utility>

#include "apps/mozart/services/input/input_device_registry.fidl.h"
#include "apps/mozart/services/input/input_reports.fidl.h"
#include "apps/mozart/src/input_reader/input_interpreter.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/io/device_watcher.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace mozart {
namespace input {

class InputReader : mtl::MessageLoopHandler {
public:
  InputReader(mozart::InputDeviceRegistry* registry);
  ~InputReader();
  void Start();

 private:
  class DeviceInfo {
   public:
    DeviceInfo(std::unique_ptr<InputInterpreter> interpreter,
               mtl::MessageLoop::HandlerKey key);
    ~DeviceInfo();

    InputInterpreter* interpreter() { return interpreter_.get(); }
    mtl::MessageLoop::HandlerKey key() { return key_; };

   private:
    std::unique_ptr<InputInterpreter> interpreter_;
    mtl::MessageLoop::HandlerKey key_;

    FTL_DISALLOW_COPY_AND_ASSIGN(DeviceInfo);
  };

  void DeviceAdded(std::unique_ptr<InputInterpreter> interpreter);
  void DeviceRemoved(mx_handle_t handle);

  void OnDirectoryHandleReady(mx_handle_t handle, mx_signals_t pending);
  void OnDeviceHandleReady(mx_handle_t handle, mx_signals_t pending);

  void OnInternalReport(mx_handle_t handle, InputInterpreter::ReportType type);

  // |mtl::MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending);

  mozart::InputDeviceRegistry* registry_;

  std::map<mx_handle_t, std::unique_ptr<DeviceInfo>> devices_;
  std::unique_ptr<mtl::DeviceWatcher> device_watcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_INPUT_READER_INPUT_READER_H_
