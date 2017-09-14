// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/vfs/vfs_handler.h"

#include <fdio/remoteio.h>

#include "lib/fsl/vfs/vfs_dispatcher.h"

namespace fsl {

VFSHandler::VFSHandler(VFSDispatcher* dispatcher)
    : dispatcher_(dispatcher), key_(0), iostate_(nullptr) {
  FXL_DCHECK(dispatcher_);
}

VFSHandler::~VFSHandler() {
  if (key_) {
    fsl::MessageLoop::GetCurrent()->RemoveHandler(key_);
    key_ = 0;
    zxrio_handler(ZX_HANDLE_INVALID, (void*)callback_, iostate_);
  }
}

void VFSHandler::Start(zx::channel channel,
                       fs::vfs_dispatcher_cb_t callback,
                       void* iostate) {
  FXL_DCHECK(!channel_);
  channel_ = std::move(channel);
  callback_ = callback;
  iostate_ = iostate;
  key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
      this, channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
}

void VFSHandler::OnHandleReady(zx_handle_t handle,
                               zx_signals_t pending,
                               uint64_t count) {
  if (pending & ZX_CHANNEL_READABLE) {
    zx_status_t status =
        zxrio_handler(channel_.get(), (void*)callback_, iostate_);
    if (status == ZX_OK)
      return;
    Stop(status < 0);
  } else {
    FXL_DCHECK(pending & ZX_CHANNEL_PEER_CLOSED);
    Stop(true);
  }
}

void VFSHandler::OnHandleError(zx_handle_t handle, zx_status_t error) {
  FXL_DLOG(ERROR) << "VFSHandler::OnHandleError error=" << error;
  Stop(true);
}

void VFSHandler::Stop(bool needs_close) {
  FXL_DCHECK(key_);
  fsl::MessageLoop::GetCurrent()->RemoveHandler(key_);
  key_ = 0;
  if (needs_close)
    zxrio_handler(ZX_HANDLE_INVALID, (void*)callback_, iostate_);
  dispatcher_->Stop(this);
  // We're deleted now.
}

}  // namespace fsl
