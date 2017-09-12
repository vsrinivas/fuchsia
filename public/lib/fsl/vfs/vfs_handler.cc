// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/vfs/vfs_handler.h"

#include <mxio/remoteio.h>

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
    mxrio_handler(MX_HANDLE_INVALID, (void*)callback_, iostate_);
  }
}

void VFSHandler::Start(mx::channel channel,
                       fs::vfs_dispatcher_cb_t callback,
                       void* iostate) {
  FXL_DCHECK(!channel_);
  channel_ = std::move(channel);
  callback_ = callback;
  iostate_ = iostate;
  key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
      this, channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
}

void VFSHandler::OnHandleReady(mx_handle_t handle,
                               mx_signals_t pending,
                               uint64_t count) {
  if (pending & MX_CHANNEL_READABLE) {
    mx_status_t status =
        mxrio_handler(channel_.get(), (void*)callback_, iostate_);
    if (status == MX_OK)
      return;
    Stop(status < 0);
  } else {
    FXL_DCHECK(pending & MX_CHANNEL_PEER_CLOSED);
    Stop(true);
  }
}

void VFSHandler::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FXL_DLOG(ERROR) << "VFSHandler::OnHandleError error=" << error;
  Stop(true);
}

void VFSHandler::Stop(bool needs_close) {
  FXL_DCHECK(key_);
  fsl::MessageLoop::GetCurrent()->RemoveHandler(key_);
  key_ = 0;
  if (needs_close)
    mxrio_handler(MX_HANDLE_INVALID, (void*)callback_, iostate_);
  dispatcher_->Stop(this);
  // We're deleted now.
}

}  // namespace fsl
