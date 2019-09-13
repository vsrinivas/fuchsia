// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/mutex.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls/log.h>

#include <array>

#include "private.h"

#define LOGBUF_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

// A |zxio_t| backend that uses a debuglog.
//
// The |handle| handle is a Zircon debuglog object.
typedef struct zxio_debuglog {
  zxio_t io;
  zx::debuglog handle;
  sync_mutex_t lock;
  struct {
    unsigned next;
    std::unique_ptr<std::array<char, LOGBUF_MAX>> pending;
  } buffer __TA_GUARDED(lock);
} zxio_debuglog_t;

static_assert(sizeof(zxio_debuglog_t) <= sizeof(zxio_storage_t),
              "zxio_debuglog_t must fit inside zxio_storage_t.");

static zx_status_t zxio_debuglog_close(zxio_t* io) {
  auto debuglog = reinterpret_cast<zxio_debuglog_t*>(io);
  debuglog->~zxio_debuglog_t();
  return ZX_OK;
}

zx_status_t zxio_debuglog_clone(zxio_t* io, zx_handle_t* out_handle) {
  auto debuglog = reinterpret_cast<zxio_debuglog_t*>(io);
  zx::debuglog handle;
  zx_status_t status = debuglog->handle.duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  *out_handle = handle.release();
  return status;
}

static zx_status_t zxio_debuglog_write_vector(zxio_t* io, const zx_iovec_t* vector,
                                              size_t vector_count, zxio_flags_t flags,
                                              size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto debuglog = reinterpret_cast<zxio_debuglog_t*>(io);

  auto& outgoing = debuglog->buffer;
  sync_mutex_lock(&debuglog->lock);
  if (outgoing.pending == nullptr) {
    outgoing.pending = std::make_unique<std::array<char, LOGBUF_MAX>>();
  }

  zx_status_t status = zxio_do_vector(
      vector, vector_count, out_actual, [&](void* buffer, size_t capacity, size_t* out_actual) {
        // Convince the compiler that the lock is held here. This is safe
        // because the lock is held over the call to zxio_do_vector and
        // zxio_do_vector is synchronous, so it cannot extend the life of this
        // lambda.
        //
        // This is required because the compiler does its analysis
        // function-by-function. In this function, it can't see that the lock is
        // held by the calling scope. If We annotate this function with
        // TA_REQUIRES, then this function compiles, but its call in
        // zxio_do_vector doesn't, because the compiler can't tell that the lock
        // is held in that function.
        []() __TA_ASSERT(debuglog->lock) {}();
        const char* data = static_cast<const char*>(buffer);
        for (size_t i = 0; i < capacity; ++i) {
          char c = *data++;
          if (c == '\n') {
            debuglog->handle.write(0, outgoing.pending->data(), outgoing.next);
            outgoing.next = 0;
            continue;
          }
          if (c < ' ') {
            continue;
          }
          (*outgoing.pending)[outgoing.next++] = c;
          if (outgoing.next == LOGBUF_MAX) {
            debuglog->handle.write(0, outgoing.pending->data(), outgoing.next);
            outgoing.next = 0;
            continue;
          }
        }
        *out_actual = capacity;
        return ZX_OK;
      });
  sync_mutex_unlock(&debuglog->lock);
  return status;
}

static zx_status_t zxio_debuglog_isatty(zxio_t* io, bool* tty) {
  // debuglog needs to be a tty in order to tell stdio
  // to use line-buffering semantics - bunching up log messages
  // for an arbitrary amount of time makes for confusing results!
  *tty = true;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_debuglog_ops = ([]() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_debuglog_close;
  ops.clone = zxio_debuglog_clone;
  ops.write_vector = zxio_debuglog_write_vector;
  ops.isatty = zxio_debuglog_isatty;
  return ops;
})();

zx_status_t zxio_debuglog_init(zxio_storage_t* storage, zx::debuglog handle) {
  auto debuglog = new (storage) zxio_debuglog_t{
      .io = storage->io,
      .handle = std::move(handle),
      .lock = {},
      .buffer = {},
  };
  zxio_init(&debuglog->io, &zxio_debuglog_ops);
  return ZX_OK;
}
