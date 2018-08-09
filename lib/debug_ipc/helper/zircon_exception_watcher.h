// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

namespace debug_ipc {

// Callbacks for exceptions from a process exception port.
class ZirconExceptionWatcher {
 public:
  virtual void OnProcessTerminated(zx_koid_t process_koid) {}
  virtual void OnThreadStarting(zx_koid_t process_koid,
                                zx_koid_t thread_koid) {}
  virtual void OnThreadExiting(zx_koid_t process_koid, zx_koid_t thread_koid) {}
  virtual void OnException(zx_koid_t process_koid, zx_koid_t thread_koid,
                           uint32_t type) {}
};

}  // namespace debug_ipc
