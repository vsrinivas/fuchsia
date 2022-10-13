// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/cpp/transitional.h>
#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <poll.h>
#include <sys/socket.h>
#include <zircon/types.h>

#include <algorithm>

zx_status_t zxio_sendmsg_inner(zxio_t* io, const struct msghdr* msg, int flags,
                               size_t* out_actual) {
  if (flags) {
    // TODO(https://fxbug.dev/67925): support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Variable length arrays have to have nonzero sizes, so we can't allocate a zx_iov for an empty
  // io vector. Instead, we can allocate 1 entry when msg_iovlen is 0.  This way zx_iov can still be
  // non-nullptr (for example, to avoid failure due to vector nullptr when calling
  // zx_stream_writev() further down; more generally so implementations further down don't need to
  // worry about allowing zx_iovec_t* nullptr but only when vector_count == 0), but we won't
  // actually use the 1 entry since msg->msg_iovlen is zero.
  zx_iovec_t zx_iov[std::max(msg->msg_iovlen, 1)];

  for (int i = 0; i < msg->msg_iovlen; ++i) {
    zx_iov[i] = {
        .buffer = msg->msg_iov[i].iov_base,
        .capacity = msg->msg_iov[i].iov_len,
    };
  }
  return zxio_writev(io, zx_iov, msg->msg_iovlen, 0, out_actual);
}

zx_status_t zxio_recvmsg_inner(zxio_t* io, struct msghdr* msg, int flags, size_t* out_actual) {
  zxio_flags_t zxio_flags = 0;
  if (flags & MSG_PEEK) {
    zxio_flags |= ZXIO_PEEK;
    flags &= ~MSG_PEEK;
  }
  if (flags) {
    // TODO(https://fxbug.dev/67925): support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Variable length arrays have to have nonzero sizes, so we can't allocate a zx_iov for an empty
  // io vector. Instead, we can allocate 1 entry when msg_iovlen is 0.  This way zx_iov can still be
  // non-nullptr (for example, to avoid failure due to vector nullptr when calling
  // zx_stream_readv() further down; more generally so implementations further down don't need to
  // worry about allowing zx_iovec_t* nullptr but only when vector_count == 0), but we won't
  // actually use the 1 entry since msg->msg_iovlen is zero.
  zx_iovec_t zx_iov[std::max(msg->msg_iovlen, 1)];

  for (int i = 0; i < msg->msg_iovlen; ++i) {
    iovec const& iov = msg->msg_iov[i];
    zx_iov[i] = {
        .buffer = iov.iov_base,
        .capacity = iov.iov_len,
    };
  }

  return zxio_readv(io, zx_iov, msg->msg_iovlen, zxio_flags, out_actual);
}

// TODO(https://fxbug.dev/45813): This is mainly used by pipes. Consider merging this with the
// POSIX-to-zxio signal translation in |remote::wait_begin|.
// TODO(https://fxbug.dev/47132): Do not change the signal mapping here and in |wait_end|
// until linked issue is resolved.
void zxio_wait_begin_inner(zxio_t* io, uint32_t events, zx_signals_t signals,
                           zx_handle_t* out_handle, zx_signals_t* out_signals) {
  if (events & POLLIN) {
    signals |= ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_PEER_CLOSED | ZXIO_SIGNAL_READ_DISABLED;
  }
  if (events & POLLOUT) {
    signals |= ZXIO_SIGNAL_WRITABLE | ZXIO_SIGNAL_WRITE_DISABLED;
  }
  if (events & POLLRDHUP) {
    signals |= ZXIO_SIGNAL_READ_DISABLED | ZXIO_SIGNAL_PEER_CLOSED;
  }
  zxio_wait_begin(io, signals, out_handle, out_signals);
}

void zxio_wait_end_inner(zxio_t* io, zx_signals_t signals, uint32_t* out_events,
                         zx_signals_t* out_signals) {
  zxio_signals_t zxio_signals;
  zxio_wait_end(io, signals, &zxio_signals);
  if (out_signals) {
    *out_signals = zxio_signals;
  }

  uint32_t events = 0;
  if (zxio_signals & (ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_PEER_CLOSED | ZXIO_SIGNAL_READ_DISABLED)) {
    events |= POLLIN;
  }
  if (zxio_signals & (ZXIO_SIGNAL_WRITABLE | ZXIO_SIGNAL_WRITE_DISABLED)) {
    events |= POLLOUT;
  }
  if (zxio_signals & (ZXIO_SIGNAL_READ_DISABLED | ZXIO_SIGNAL_PEER_CLOSED)) {
    events |= POLLRDHUP;
  }
  *out_events = events;
}
