// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/event.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/eventfd.h>
#include <zircon/assert.h>

#include <algorithm>

#include <fbl/auto_lock.h>

#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/zxio.h"

constexpr auto kSignalReadable =
    static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable);
constexpr auto kSignalWritable =
    static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable);

// An implementation of a POSIX eventfd.
struct fdio_event_t {
  zxio_t io;

  zx::event handle;

  mtx_t lock;
  eventfd_t value __TA_GUARDED(lock);
  int flags __TA_GUARDED(lock);
};

static_assert(sizeof(fdio_event_t) <= sizeof(zxio_storage_t),
              "fdio_event_t must fit inside zxio_storage_t.");

static zx_status_t fdio_event_close(zxio_t* io) {
  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);
  event->handle.reset();
  return ZX_OK;
}

static void fdio_event_update_signals(fdio_event_t* event) __TA_REQUIRES(event->lock) {
  zx_signals_t set_mask = ZX_SIGNAL_NONE;
  if (event->value > 0) {
    set_mask |= kSignalReadable;
  }
  if (event->value < UINT64_MAX - 1) {
    set_mask |= kSignalWritable;
  }
  zx_status_t status = event->handle.signal(kSignalReadable | kSignalWritable, set_mask);
  ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));
}

static zx_status_t fdio_event_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual) {
  if (fdio_iovec_get_capacity(vector, vector_count) < sizeof(uint64_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);

  fbl::AutoLock lock(&event->lock);
  if (event->value == 0u) {
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

  fbl::AutoLock lock(&event->lock);
  uint64_t new_value = 0u;
  if (add_overflow(event->value, increment, &new_value) || new_value == UINT64_MAX) {
    // If we overflow, we need to block until the next read, which means we need to clear the
    // writable signal. The next read is not guaranteed to make enough room for this write, but
    // the documentation says we should wake up and try again regardless.
    //
    // This design has an observable difference from Linux. If you make a write()
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
    zx_status_t status = event->handle.signal(kSignalWritable, ZX_SIGNAL_NONE);
    ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));

    return ZX_ERR_SHOULD_WAIT;
  }

  event->value = new_value;

  fdio_event_update_signals(event);
  *out_actual = actual;
  return ZX_OK;
}

static void fdio_event_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                                  zx_signals_t* out_zx_signals) {
  fdio_event_t* event = reinterpret_cast<fdio_event_t*>(io);
  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= kSignalReadable;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= kSignalWritable;
  }
  *out_handle = event->handle.get();
  *out_zx_signals = zx_signals;
}

static void fdio_event_wait_end(zxio_t* io, zx_signals_t zx_signals,
                                zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & kSignalReadable) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & kSignalWritable) {
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

__EXPORT
int eventfd(unsigned int initval, int flags) {
  if (flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)) {
    return ERRNO(EINVAL);
  }

  zx::event handle;
  zx_status_t status = zx::event::create(0, &handle);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  zx::result io = fdio_internal::zxio::create();
  if (io.is_error()) {
    return ERROR(io.status_value());
  }

  fdio_event_t* event = new (&io->zxio_storage()) fdio_event_t{
      .handle = std::move(handle),
      .value = initval,
      .flags = flags,
  };
  zxio_init(&event->io, &fdio_event_ops);
  {
    fbl::AutoLock lock(&event->lock);
    fdio_event_update_signals(event);
  }

  if (flags & EFD_CLOEXEC) {
    io->ioflag() |= IOFLAG_CLOEXEC;
  }

  if (flags & EFD_NONBLOCK) {
    io->ioflag() |= IOFLAG_NONBLOCK;
  }

  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
}

__EXPORT
int eventfd_read(int fd, eventfd_t* value) {
  return read(fd, value, sizeof(eventfd_t)) != sizeof(eventfd_t) ? -1 : 0;
}

__EXPORT
int eventfd_write(int fd, eventfd_t value) {
  return write(fd, &value, sizeof(eventfd_t)) != sizeof(eventfd_t) ? -1 : 0;
}
