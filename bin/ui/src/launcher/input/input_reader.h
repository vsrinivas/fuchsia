// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_READER_H_
#define APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_READER_H_

#include <mx/channel.h>

#include <map>
#include <utility>

#include "apps/mozart/src/launcher/input/input_interpreter.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"
#include "mojo/public/cpp/system/handle.h"

namespace mozart {
namespace input {

class InputReader : mtl::MessageLoopHandler {
 public:
  InputReader(InputInterpreter* interpreter);
  ~InputReader();
  void Start();

 private:
  void MonitorDirectory();
  void DeviceAdded(std::unique_ptr<InputDevice> device);
  void DeviceRemoved(mx_handle_t handle);

  void OnDirectoryHandleReady(mx_handle_t handle);
  void OnDeviceHandleReady(mx_handle_t handle);

  // |mtl::MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle);
  void OnHandleError(mx_handle_t handle, mx_status_t result);

  InputInterpreter* interpreter_;

  mtl::MessageLoop* main_loop_;
  mtl::MessageLoop::HandlerKey input_directory_key_;
  int input_directory_fd_;
  mx::channel input_directory_channel_;

  std::map<
      mx_handle_t,
      std::pair<std::unique_ptr<InputDevice>, mtl::MessageLoop::HandlerKey>>
      devices_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_READER_H_
