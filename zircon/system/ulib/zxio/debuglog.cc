// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/mutex.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <array>

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

static zx_status_t zxio_debuglog_write(zxio_t* io, const void* buffer, size_t capacity,
                                       size_t* out_actual) {
  auto debuglog = reinterpret_cast<zxio_debuglog_t*>(io);

  auto& outgoing = debuglog->buffer;
  sync_mutex_lock(&debuglog->lock);
  if (outgoing.pending == nullptr) {
    outgoing.pending = std::make_unique<std::array<char, LOGBUF_MAX>>();
  }

  auto data = static_cast<const char*>(buffer);
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
  sync_mutex_unlock(&debuglog->lock);
  *out_actual = capacity;
  return ZX_OK;
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
  ops.write = zxio_debuglog_write;
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
