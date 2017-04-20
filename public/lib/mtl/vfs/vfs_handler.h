// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VFS_VFS_HANDLER_H_
#define LIB_MTL_VFS_VFS_HANDLER_H_

#include <mx/channel.h>

#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace mtl {

class VFSHandler : public MessageLoopHandler {
 public:
  // Implements mxio_dispatcher_cb_t.
  static mx_status_t Start(mx_handle_t channel, void* callback, void* cookie);

 private:
  VFSHandler(mx::channel channel, void* callback, void* cookie);
  ~VFSHandler() override;

  // |MessageLoopHandler| implementation:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  void Stop(bool needs_close);

  MessageLoop::HandlerKey key_;
  mx::channel channel_;
  void* callback_;
  void* cookie_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VFSHandler);
};

}  // namespace mtl

#endif  // LIB_MTL_VFS_VFS_HANDLER_H_
