// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_MESSAGE_LOOP_HANDLER_H_
#define LIB_MTL_TASKS_MESSAGE_LOOP_HANDLER_H_

#include <magenta/types.h>

namespace mtl {

class MessageLoopHandler {
 public:
  virtual void OnHandleReady(mx_handle_t handle) = 0;
  virtual void OnHandleError(mx_handle_t handle, mx_status_t result) = 0;

 protected:
  virtual ~MessageLoopHandler();
};

}  // namespace mtl

#endif  // LIB_MTL_TASKS_MESSAGE_LOOP_HANDLER_H_
