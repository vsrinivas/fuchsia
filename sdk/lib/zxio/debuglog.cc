// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls/log.h>

#include <array>
#include <mutex>

#include "private.h"

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
    std::array<char, ZX_LOG_RECORD_DATA_MAX> pending;
    decltype(pending)::iterator it;
  };

  zx::debuglog handle_;
  std::mutex lock_;
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

  std::lock_guard lock(lock_);
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
    if (data == nullptr && capacity == 0) {
      // The musl (libc) layer above uses f->write(f, 0, 0) to indicate the need for a
      // flush-out-of-process (when client code does fflush(FILE), and in a few other cases).  That
      // call will result in vector[i].buffer == nullptr && vector[i].capacity == 0 at this layer.
      //
      // We check for both because we don't want to hide / normalize data == nullptr with capacity
      // not 0, and we don't want to trigger on data != nullptr and capacity == 0 since that might
      // match on additional cases beyond the f->write(f, 0, 0) data == nullptr && capacity == 0
      // case.
      //
      // We currently know that the (nullptr, 0; flush) entry, when present, will be the last entry,
      // but this implementation can handle additional batched write entries beyond the flush
      // without incorrectly implying flush of the later batched write entries.  Such additional
      // batched write entries beyond the flush seem unlikely to be added in future, but since
      // allowing for them isn't really any additional cost, we allow for them here.
      //
      // Since no layer above is filtering out data == nullptr && capacity == 0 passed by client
      // code in a vector to writev(), a flush at this layer is reachable by client code that calls
      // writev() (or sendmsg() or similar), which seems fine, especially given that only the
      // Debuglog currently needs to do anything with such a flush - it's just ignored by other zxio
      // implementations since their fd-layer writes aren't buffered in-process.
      //
      // A (nullptr, 0) entry will flush out previously-buffered data, even if the incoming vector
      // doesn't have \n at the end of previosuly-buffered data (for example: ZX_PANIC("foo")
      // without \n after "foo").
      zx_status_t status = flush();
      if (status != ZX_OK) {
        return status;
      }
    }
    // If the above if condition is true, the capacity will be 0 so this loop won't execute its
    // body.  We could have the loop under an "else" to (at least nominally / syntactically) avoid
    // evaluating the for loop termination condition since we know it'll be false anyway when the if
    // condition above was true, but we choose instead to indent this loop less.
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
