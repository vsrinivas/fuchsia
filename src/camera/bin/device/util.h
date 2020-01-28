// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_UTIL_H_
#define SRC_CAMERA_BIN_DEVICE_UTIL_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <zircon/status.h>

// Safely unbinds a client connection, doing so on the connection's thread if it differs from the
// caller's thread.
template <class T>
inline zx_status_t Unbind(fidl::InterfacePtr<T>& p) {
  if (!p) {
    return ZX_OK;
  }

  if (p.dispatcher() == async_get_default_dispatcher()) {
    p.Unbind();
    return ZX_OK;
  }

  return async::PostTask(p.dispatcher(), [&]() { p.Unbind(); });
}

#endif  // SRC_CAMERA_BIN_DEVICE_UTIL_H_
