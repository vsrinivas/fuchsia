// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_PAVER_CONTEXT_H_
#define SRC_STORAGE_LIB_PAVER_PAVER_CONTEXT_H_

#include <lib/zx/status.h>
#include <zircon/compiler.h>

#include <memory>
#include <mutex>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace paver {
// class ContextBase and class Context aim to provide a generic mechanism for
// updating and sharing board-specific context information.
// The context itself is hosted in the paver service but up to the board-specific
// device partitioners to interpret, initialize and update.
// Since there may be multiple clients at the same time, it is important to use
// the provided Context.lock when updating context to prevent data race.
class ContextBase {
 public:
  virtual ~ContextBase() = default;
};

// The context wrapper
class Context {
 public:
  template <typename T>
  zx::status<> Initialize(std::function<zx::status<std::unique_ptr<T>>()> factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Already holds a context
    if (impl_) {
      return zx::ok();
    }
    if (auto status = factory(); status.is_ok()) {
      impl_ = std::move(status.value());
      return zx::ok();
    } else {
      return status.take_error();
    }
  }

  // All functions using the contexts are callbacks so we can grab the
  // lock and do type checking ourselves internally.
  template <typename T>
  zx::status<> Call(std::function<zx::status<>(T*)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!impl_) {
      fprintf(stderr, "Context is not initialized.\n");
      return zx::error(ZX_ERR_INTERNAL);
    }
    return callback(static_cast<T*>(impl_.get()));
  }

  template <typename T, typename R>
  zx::status<R> Call(std::function<zx::status<R>(T*)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!impl_) {
      fprintf(stderr, "Context is not initialized.\n");
      return zx::error(ZX_ERR_INTERNAL);
    }
    return callback(static_cast<T*>(impl_.get()));
  }

 private:
  std::mutex mutex_;
  std::unique_ptr<ContextBase> impl_ __TA_GUARDED(mutex_);
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_PAVER_CONTEXT_H_
