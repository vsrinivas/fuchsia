// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls/log.h>

#include <array>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "private.h"

#define LOGBUF_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

namespace {

// A |zxio_t| backend that uses a debuglog.
//
// The |handle| handle is a Zircon debuglog object.
class Debuglog : public HasIo {
 public:
  explicit Debuglog(zx::debuglog debuglog) : HasIo(kOps), handle_(std::move(debuglog)) {}

 private:
  zx_status_t Close();
  zx_status_t Clone(zx_handle_t* out_handle);
  zx_status_t Writev(const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                     size_t* out_actual);
  zx_status_t IsATty(bool* tty);

  static const zxio_ops_t kOps;

  struct Buffer {
    std::array<char, LOGBUF_MAX> pending;
    decltype(pending)::iterator it;
  };

  zx::debuglog handle_;
  fbl::Mutex lock_;
  std::unique_ptr<Buffer> buffer_ __TA_GUARDED(lock_);
};

constexpr zxio_ops_t Debuglog::kOps = ([]() {
  using Adaptor = Adaptor<Debuglog>;
  zxio_ops_t ops = zxio_default_ops;
  ops.close = Adaptor::From<&Debuglog::Close>;
  ops.clone = Adaptor::From<&Debuglog::Clone>;
  ops.writev = Adaptor::From<&Debuglog::Writev>;
  ops.isatty = Adaptor::From<&Debuglog::IsATty>;
  return ops;
})();

zx_status_t Debuglog::Close() {
  this->~Debuglog();
  return ZX_OK;
}

zx_status_t Debuglog::Clone(zx_handle_t* out_handle) {
  zx::debuglog handle;
  zx_status_t status = handle_.duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  *out_handle = handle.release();
  return status;
}

zx_status_t Debuglog::Writev(const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                             size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&lock_);
  if (buffer_ == nullptr) {
    buffer_ = std::make_unique<Buffer>();
    buffer_->it = buffer_->pending.begin();
  }
  Buffer& outgoing = *buffer_;

  auto flush = [&]() __TA_REQUIRES(lock_) {
    zx_status_t status =
        handle_.write(0, outgoing.pending.data(), outgoing.it - outgoing.pending.begin());
    outgoing.it = outgoing.pending.begin();
    return status;
  };

  auto write = [&](void* buffer, size_t capacity, size_t* out_actual) {
    // Convince the compiler that the lock is held here. This is safe because the lock is held over
    // the call to zxio_do_vector and zxio_do_vector is synchronous, so it cannot extend the life of
    // this lambda.
    //
    // This is required because the compiler does its analysis function-by-function. In this
    // function, it can't see that the lock is held by the calling scope. If We annotate this
    // function with TA_REQUIRES, then this function compiles, but its call in zxio_do_vector
    // doesn't, because the compiler can't tell that the lock is held in that function.
    []() __TA_ASSERT(lock_) {}();
    const char* data = static_cast<const char*>(buffer);
    for (size_t i = 0; i < capacity; ++i) {
      char c = data[i];
      if (c == '\n') {
        zx_status_t status = flush();
        if (status != ZX_OK) {
          return status;
        }
        continue;
      }
      if (c < ' ') {
        continue;
      }
      *outgoing.it = c;
      ++outgoing.it;
      if (outgoing.it == outgoing.pending.end()) {
        zx_status_t status = flush();
        if (status != ZX_OK) {
          return status;
        }
        continue;
      }
    }
    *out_actual = capacity;
    return ZX_OK;
  };

  return zxio_do_vector(vector, vector_count, out_actual, write);
}

zx_status_t Debuglog::IsATty(bool* tty) {
  // debuglog needs to be a tty in order to tell stdio
  // to use line-buffering semantics - bunching up log messages
  // for an arbitrary amount of time makes for confusing results!
  *tty = true;
  return ZX_OK;
}

}  // namespace

zx_status_t zxio_debuglog_init(zxio_storage_t* storage, zx::debuglog handle) {
  new (storage) Debuglog(std::move(handle));
  return ZX_OK;
}
