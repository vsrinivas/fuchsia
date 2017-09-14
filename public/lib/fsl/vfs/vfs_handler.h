// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_VFS_VFS_HANDLER_H_
#define LIB_FSL_VFS_VFS_HANDLER_H_

#include <fs/dispatcher.h>
#include <zx/channel.h>

#include "lib/fxl/fxl_export.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace fsl {
class VFSDispatcher;

class FXL_EXPORT VFSHandler : public MessageLoopHandler {
 public:
  explicit VFSHandler(VFSDispatcher* dispatcher);
  ~VFSHandler() override;

  void Start(zx::channel channel,
             fs::vfs_dispatcher_cb_t callback,
             void* iostate);

 private:
  // |MessageLoopHandler| implementation:
  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override;
  void OnHandleError(zx_handle_t handle, zx_status_t error) override;

  void Stop(bool needs_close);

  VFSDispatcher* dispatcher_;
  MessageLoop::HandlerKey key_;
  zx::channel channel_;
  fs::vfs_dispatcher_cb_t callback_;
  void* iostate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VFSHandler);
};

}  // namespace fsl

#endif  // LIB_FSL_VFS_VFS_HANDLER_H_
