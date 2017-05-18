// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/vfs/vfs_handler.h"

#include <mxio/remoteio.h>

namespace mtl {

VFSDispatcher::VFSDispatcher() = default;

mx_status_t VFSDispatcher::AddVFSHandler(mx_handle_t channel, void* callback,
                                         void* iostate) {
  return VFSHandler::Start(channel, callback, iostate);
}

VFSHandler::VFSHandler(mx::channel channel, void* callback, void* cookie)
  : key_(0),
    channel_(std::move(channel)),
    callback_(callback),
    cookie_(cookie) {
}

VFSHandler::~VFSHandler() {
  FTL_DCHECK(!key_);
}

mx_status_t VFSHandler::Start(mx_handle_t channel, void* callback, void* cookie) {
  VFSHandler* handler = new VFSHandler(mx::channel(channel), callback, cookie);
  handler->key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      handler,
      handler->channel_.get(),
      MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
  return NO_ERROR;
}

void VFSHandler::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (pending & MX_CHANNEL_READABLE) {
    mx_status_t status = mxrio_handler(channel_.get(), callback_, cookie_);
    if (status == NO_ERROR)
      return;
    Stop(status < 0);
  } else {
    FTL_DCHECK(pending & MX_CHANNEL_PEER_CLOSED);
    Stop(true);
  }
}

void VFSHandler::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_DLOG(ERROR) << "VFSHandler::OnHandleError error=" << error;
  Stop(true);
}

void VFSHandler::Stop(bool needs_close) {
  FTL_DCHECK(key_);
  mtl::MessageLoop::GetCurrent()->RemoveHandler(key_);
  key_ = 0;
  if (needs_close)
    mxrio_handler(MX_HANDLE_INVALID, callback_, cookie_);
  delete this;
}

}  // namespace mtl
