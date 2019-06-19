// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_ZIRCON_EXCEPTION_WATCHER_H_
#define SRC_DEVELOPER_DEBUG_SHARED_ZIRCON_EXCEPTION_WATCHER_H_

#include <zircon/types.h>

#include <zircon/syscalls/exception.h>
#include <lib/zx/exception.h>

namespace debug_ipc {

// Callbacks for exceptions from a process exception port.
class ZirconExceptionWatcher {
 public:
  virtual void OnProcessStarting(zx_koid_t job_koid, zx_koid_t process_koid,
                                 zx_koid_t thread_koid) {}
  virtual void OnProcessTerminated(zx_koid_t process_koid) {}
  virtual void OnThreadStarting(zx_koid_t process_koid, zx_koid_t thread_koid) {
  }
  virtual void OnThreadExiting(zx_koid_t process_koid, zx_koid_t thread_koid) {}
  virtual void OnException(zx_koid_t process_koid, zx_koid_t thread_koid,
                           uint32_t type) {}

  // New exception handling that uses the exception tokens.
  virtual void OnException(zx::exception&& exception_token,
                           zx_exception_info_t exception_info) {}
};

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_ZIRCON_EXCEPTION_WATCHER_H_
