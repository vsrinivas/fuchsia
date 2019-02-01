// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if !defined(__Fuchsia__)
#error socket_watcher.h can only be included on Fuchsia.
#endif

#include <zircon/types.h>

namespace debug_ipc {

class SocketWatcher {
 public:
  virtual void OnSocketReadable(zx_handle_t socket_handle) {}
  virtual void OnSocketWritable(zx_handle_t socket_handle) {}
  virtual void OnSocketError(zx_handle_t socket_handle) {}
};

}  // namespace debug_ipc
