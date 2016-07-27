// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_MESSAGE_LOOP_HANDLER_H_
#define LIB_MTL_TASKS_MESSAGE_LOOP_HANDLER_H_

#include "mojo/public/c/system/handle.h"

namespace mtl {

class MessageLoopHandler {
 public:
  virtual void OnHandleReady(MojoHandle handle) = 0;
  virtual void OnHandleError(MojoHandle handle, MojoResult result) = 0;

 protected:
  virtual ~MessageLoopHandler();
};

}  // namespace mtl

#endif  // LIB_MTL_TASKS_MESSAGE_LOOP_HANDLER_H_
