// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/vfs/vfs_dispatcher.h"

#include <mxio/remoteio.h>

#include "lib/mtl/vfs/vfs_handler.h"

namespace mtl {

VFSDispatcher::VFSDispatcher() = default;

VFSDispatcher::~VFSDispatcher() {
  for (auto handler : std::move(handlers_))
    delete handler;
}

mx_status_t VFSDispatcher::AddVFSHandler(mx_handle_t channel,
                                         void* callback,
                                         void* iostate) {
  VFSHandler* handler = new VFSHandler(this);
  handler->Start(mx::channel(channel), callback, iostate);
  handlers_.insert(handler);
  return MX_OK;
}

void VFSDispatcher::Stop(VFSHandler* handler) {
  handlers_.erase(handler);
  delete handler;
}

}  // namespace mtl
