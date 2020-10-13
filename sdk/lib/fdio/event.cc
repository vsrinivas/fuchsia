// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sync/mutex.h>
#include <lib/zx/event.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/eventfd.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <utility>

#include "internal.h"
#include "unistd.h"

namespace fio2 = llcpp::fuchsia::io2;

#define FDIO_EVENT_READABLE static_cast<zx_signals_t>(fio2::DeviceSignal::READABLE)
#define FDIO_EVENT_WRITABLE static_cast<zx_signals_t>(fio2::DeviceSignal::WRITABLE)

// An implementation of a POSIX eventfd.
typedef struct fdio_event {
  zxio_t io;

  // The zx::event object that implements the eventfd.
  zx_handle_t handle;

  sync_mutex_t lock;
  eventfd_t value __TA_GUARDED(lock);
  int flags __TA_GUARDED(lock);
} fdio_event_t;

static_assert(sizeof(fdio_event_t) <= sizeof(zxio_storage_t),
              "fdio_event_t must fit inside zxio_storage_t.");

static zx_status_t fdio_event_close(zxio_t* io) {
  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);
  zx_handle_t handle = event->handle;
  event->handle = ZX_HANDLE_INVALID;
  zx_handle_close(handle);
  return ZX_OK;
}

static void fdio_event_update_signals(fdio_event_t* event) __TA_REQUIRES(event->lock) {
  zx_signals_t set_mask = ZX_SIGNAL_NONE;
  if (event->value > 0) {
    set_mask |= FDIO_EVENT_READABLE;
  }
  if (event->value < UINT64_MAX - 1) {
    set_mask |= FDIO_EVENT_WRITABLE;
  }
  zx_status_t status =
      zx_object_signal(event->handle, FDIO_EVENT_READABLE | FDIO_EVENT_WRITABLE, set_mask);
  ZX_ASSERT(status == ZX_OK);
}

static zx_status_t fdio_event_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual) {
  if (fdio_iovec_get_capacity(vector, vector_count) < sizeof(uint64_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);

  sync_mutex_lock(&event->lock);
  if (event->value == 0u) {
    sync_mutex_unlock(&event->lock);
    return ZX_ERR_SHOULD_WAIT;
  }

  uint64_t result = 0u;
  if (event->flags & EFD_SEMAPHORE) {
    result = 1;
    event->value -= 1;
  } else {
    result = event->value;
    event->value = 0u;
  }

  fdio_iovec_copy_to(reinterpret_cast<const uint8_t*>(&result), sizeof(result), vector,
                     vector_count, out_actual);

  fdio_event_update_signals(event);
  sync_mutex_unlock(&event->lock);
  return ZX_OK;
}

static zx_status_t fdio_event_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                     zxio_flags_t flags, size_t* out_actual) {
  uint64_t increment = 0u;
  size_t actual = 0u;
  fdio_iovec_copy_from(vector, vector_count, reinterpret_cast<uint8_t*>(&increment),
                       sizeof(increment), &actual);
  if (actual != sizeof(increment)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (increment == UINT64_MAX) {
    // UINT64_MAX is specifically disallowed, presumably to avoid clients passing -1 by mistake.
    return ZX_ERR_INVALID_ARGS;
  }

  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);

  sync_mutex_lock(&event->lock);
  uint64_t new_value = 0u;
  if (add_overflow(event->value, increment, &new_value) || new_value == UINT64_MAX) {
    // If we overflow, we need to block until the next read, which means we need to clear the
    // writable signal. The next read is not guaranteed to make enough room for this write, but
    // the documentation says we should wake up and try again regardless.
    //
    // This design has an observable different from Linux. If you make a write()
    // that goes down this codepath, this call will clear the POLLOUT bit, which
    // is observable using select() and similar functions, regardless of whether
    // the write is blocking or non-blocking. The Linux implementation
    // differentiates between blocking and non-blocking writes. Blocking writes
    // block internally without clearing the POLLOUT bit.
    //
    // To match the Linux behavior exactly, we would need to plumb the
    // information about whether this is a blocking or non-blocking write to
    // this location. If the write is non-blocking, we should return
    // ZX_ERR_SHOULD_WAIT without clearing the POLLOUT bit. (Of course, this
    // will cause code that attempts to wait for POLLOUT to spin hot, but that's
    // true on Linux as well.) If the write is blocking, then we should block on
    // a sync_completion_t, which should be signaled in fdio_event_readv.
    //
    // We hope the behavior we have implemented here is sufficiently compatible
    // to be useful. If not, we might need to restructure how we do blocking
    // read and write operations (e.g., by including the "should block" flag in
    // zxio_flags_t.
    zx_status_t status = zx_object_signal(event->handle, FDIO_EVENT_WRITABLE, ZX_SIGNAL_NONE);
    ZX_ASSERT(status == ZX_OK);

    sync_mutex_unlock(&event->lock);
    return ZX_ERR_SHOULD_WAIT;
  }

  event->value = new_value;

  fdio_event_update_signals(event);
  sync_mutex_unlock(&event->lock);
  *out_actual = actual;
  return ZX_OK;
}

static void fdio_event_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                                  zx_signals_t* out_zx_signals) {
  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);
  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= FDIO_EVENT_READABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= FDIO_EVENT_WRITABLE;
  }
  *out_handle = event->handle;
  *out_zx_signals = zx_signals;
}

static void fdio_event_wait_end(zxio_t* io, zx_signals_t zx_signals,
                                zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & FDIO_EVENT_READABLE) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & FDIO_EVENT_WRITABLE) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  *out_zxio_signals = zxio_signals;
}

static constexpr zxio_ops_t fdio_event_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = fdio_event_close;
  ops.readv = fdio_event_readv;
  ops.writev = fdio_event_writev;
  ops.wait_begin = fdio_event_wait_begin;
  ops.wait_end = fdio_event_wait_end;
  return ops;
}();

static fdio_t* fdio_event_create(zx::event handle, eventfd_t initial_value, int flags) {
  zxio_storage_t* storage = nullptr;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == nullptr) {
    return nullptr;
  }
  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(storage);
  zxio_init(&event->io, &fdio_event_ops);
  event->handle = handle.release();
  event->lock = {};
  sync_mutex_lock(&event->lock);
  event->value = initial_value;
  event->flags = flags;
  fdio_event_update_signals(event);
  sync_mutex_unlock(&event->lock);
  return io;
}

__EXPORT
int eventfd(unsigned int initval, int flags) {
  if (flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)) {
    return ERRNO(EINVAL);
  }

  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  fdio_t* io = nullptr;
  if ((io = fdio_event_create(std::move(event), initval, flags)) == nullptr) {
    return ERROR(ZX_ERR_NO_MEMORY);
  }

  if (flags & EFD_CLOEXEC) {
    *fdio_get_ioflag(io) |= IOFLAG_CLOEXEC;
  }

  if (flags & EFD_NONBLOCK) {
    *fdio_get_ioflag(io) |= IOFLAG_NONBLOCK;
  }

  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_release(io);
  }
  // fdio_bind_to_fd already sets errno.
  return fd;
}

__EXPORT
int eventfd_read(int fd, eventfd_t* value) {
  return read(fd, value, sizeof(eventfd_t)) != sizeof(eventfd_t) ? -1 : 0;
}

__EXPORT
int eventfd_write(int fd, eventfd_t value) {
  return write(fd, &value, sizeof(eventfd_t)) != sizeof(eventfd_t) ? -1 : 0;
}
