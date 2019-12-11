// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/socket.h>
#include <lib/zxio/inception.h>
#include <lib/zxs/protocol.h>
#include <poll.h>
#include <zircon/syscalls.h>

#include "private-socket.h"
#include "private.h"
#include "unistd.h"

namespace fsocket = ::llcpp::fuchsia::posix::socket;

static inline zxio_socket_t* fdio_get_zxio_socket(fdio_t* io) {
  return reinterpret_cast<zxio_socket_t*>(fdio_get_zxio(io));
}

static zx_status_t zxsio_recvmsg_dgram(fdio_t* io, struct msghdr* msg, int flags,
                                       size_t* out_actual) {
  size_t maximum = 0;
  // We are going to pad the "real" message:
  // - a buffer at the front of the vector into which we'll read the header.
  // - a 1-byte buffer at the back of the vector which we'll use to determine
  //   if the response was truncated.
  int iovlen = 1 + msg->msg_iovlen + 1;
  struct iovec iov[iovlen];

  fdio_socket_msg_t header;
  iov[0] = {
      .iov_base = static_cast<void*>(&header),
      .iov_len = sizeof(header),
  };
  maximum += sizeof(header);

  std::copy_n(msg->msg_iov, msg->msg_iovlen, &iov[1]);
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    maximum += msg->msg_iov[i].iov_len;
  }

  char byte[1];
  iov[iovlen - 1] = {
      .iov_base = byte,
      .iov_len = sizeof(byte),
  };

  size_t actual;
  {
    struct msghdr padded_msg = *msg;
    padded_msg.msg_iov = iov;
    padded_msg.msg_iovlen = iovlen;
    // According to `man 2 recvfrom`:
    //
    // This flag has no effect for datagram sockets.
    flags &= ~MSG_WAITALL;
    zx_status_t status = fdio_zxio_recvmsg(io, &padded_msg, flags, &actual);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (actual < sizeof(header)) {
    return ZX_ERR_INTERNAL;
  }
  if (msg->msg_name != nullptr) {
    memcpy(msg->msg_name, &header.addr, std::min(msg->msg_namelen, header.addrlen));
  }
  msg->msg_namelen = header.addrlen;
  msg->msg_flags = header.flags;

  if (actual > maximum) {
    msg->msg_flags |= MSG_TRUNC;
    actual = maximum;
  }
  *out_actual = actual - sizeof(header);
  return ZX_OK;
}

static zx_status_t zxsio_recvmsg_stream(fdio_t* io, struct msghdr* msg, int flags,
                                        size_t* out_actual) {
  if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
    return ZX_ERR_NOT_CONNECTED;
  }
  return fdio_zxio_recvmsg(io, msg, flags, out_actual);
}

static zx_status_t zxsio_sendmsg_dgram(fdio_t* io, const struct msghdr* msg, int flags,
                                       size_t* out_actual) {
  if (msg->msg_namelen > sizeof(((fdio_socket_msg_t*)nullptr)->addr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  int iovlen = 1 + msg->msg_iovlen;
  struct iovec iov[iovlen];

  fdio_socket_msg_t header{
      .addr = {},
      .addrlen = msg->msg_namelen,
      .flags = 0,
  };
  if (msg->msg_name != nullptr) {
    memcpy(&header.addr, msg->msg_name, msg->msg_namelen);
  }
  iov[0] = {
      .iov_base = static_cast<void*>(&header),
      .iov_len = sizeof(header),
  };
  std::copy_n(msg->msg_iov, msg->msg_iovlen, &iov[1]);

  size_t actual;
  {
    struct msghdr padded_msg = *msg;
    padded_msg.msg_iov = iov;
    padded_msg.msg_iovlen = iovlen;
    zx_status_t status = fdio_zxio_sendmsg(io, &padded_msg, flags, &actual);
    if (status != ZX_OK) {
      return status;
    }
  }
  *out_actual = actual - sizeof(header);
  return ZX_OK;
}

static zx_status_t zxsio_sendmsg_stream(fdio_t* io, const struct msghdr* msg, int flags,
                                        size_t* out_actual) {
  // TODO: support flags and control messages
  if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
    return ZX_ERR_NOT_CONNECTED;
  }
  return fdio_zxio_sendmsg(io, msg, flags, out_actual);
}

static void zxsio_wait_begin_stream(fdio_t* io, uint32_t events, zx_handle_t* handle,
                                    zx_signals_t* _signals) {
  zxio_socket_t* sio = fdio_get_zxio_socket(io);
  *handle = sio->pipe.socket.get();
  // TODO: locking for flags/state
  if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTING) {
    // check the connection state
    zx_signals_t observed;
    zx_status_t status =
        sio->pipe.socket.wait_one(ZXSIO_SIGNAL_CONNECTED, zx::time::infinite_past(), &observed);
    if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
      if (observed & ZXSIO_SIGNAL_CONNECTED) {
        *fdio_get_ioflag(io) &= ~IOFLAG_SOCKET_CONNECTING;
        *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
      }
    }
  }

  zx_signals_t signals = ZX_SOCKET_PEER_CLOSED;
  if (events & (POLLOUT | POLLHUP)) {
    signals |= ZX_SOCKET_WRITE_DISABLED;
  }
  if (events & (POLLIN | POLLRDHUP)) {
    signals |= ZX_SOCKET_PEER_WRITE_DISABLED;
  }

  if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED) {
    // Can't subscribe to ZX_SOCKET_WRITABLE unless we're connected; such a subscription would
    // immediately fire, since the socket buffer is almost certainly empty.
    if (events & POLLOUT) {
      signals |= ZX_SOCKET_WRITABLE;
    }
    // This is just here for symmetry with POLLOUT above.
    if (events & POLLIN) {
      signals |= ZX_SOCKET_READABLE;
    }
  } else {
    if (events & POLLOUT) {
      // signal when connect() operation is finished.
      signals |= ZXSIO_SIGNAL_OUTGOING;
    }
    if (events & POLLIN) {
      // signal when a listening socket gets an incoming connection.
      signals |= ZXSIO_SIGNAL_INCOMING;
    }
  }
  *_signals = signals;
}

static void zxsio_wait_end_stream(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
  // check the connection state
  if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTING) {
    if (signals & ZXSIO_SIGNAL_CONNECTED) {
      *fdio_get_ioflag(io) &= ~IOFLAG_SOCKET_CONNECTING;
      *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
    }
  }
  uint32_t events = 0;
  if (signals & ZX_SOCKET_PEER_CLOSED) {
    events |= POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
  }
  if (signals & ZX_SOCKET_WRITE_DISABLED) {
    events |= POLLHUP | POLLOUT;
  }
  if (signals & ZX_SOCKET_PEER_WRITE_DISABLED) {
    events |= POLLRDHUP | POLLIN;
  }
  if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED) {
    if (signals & ZX_SOCKET_WRITABLE) {
      events |= POLLOUT;
    }
    if (signals & ZX_SOCKET_READABLE) {
      events |= POLLIN;
    }
  } else {
    if (signals & ZXSIO_SIGNAL_OUTGOING) {
      events |= POLLOUT;
    }
    if (signals & ZXSIO_SIGNAL_INCOMING) {
      events |= POLLIN;
    }
  }
  *_events = events;
}

static zx_status_t zxsio_posix_ioctl_stream(fdio_t* io, int request, va_list va) {
  return fdio_zx_socket_posix_ioctl(fdio_get_zxio_socket(io)->pipe.socket, request, va);
}

static void zxsio_wait_begin_dgram(fdio_t* io, uint32_t events, zx_handle_t* handle,
                                   zx_signals_t* _signals) {
  zxio_socket_t* sio = fdio_get_zxio_socket(io);
  *handle = sio->pipe.socket.get();
  zx_signals_t signals = ZX_SOCKET_PEER_CLOSED;
  if (events & POLLIN) {
    signals |= ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED;
  }
  if (events & POLLOUT) {
    signals |= ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED;
  }
  if (events & POLLRDHUP) {
    signals |= ZX_SOCKET_PEER_WRITE_DISABLED;
  }
  *_signals = signals;
}

static void zxsio_wait_end_dgram(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
  uint32_t events = 0;
  if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
    events |= POLLIN;
  }
  if (signals & (ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED)) {
    events |= POLLOUT;
  }
  if (signals & ZX_SOCKET_PEER_CLOSED) {
    events |= POLLERR;
  }
  if (signals & (ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
    events |= POLLRDHUP;
  }
  *_events = events;
}

static zx_status_t fdio_socket_shutdown(fdio_t* io, int how) {
  if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
    return ZX_ERR_BAD_STATE;
  }
  return fdio_zx_socket_shutdown(fdio_get_zxio_socket(io)->pipe.socket, how);
}

static fdio_ops_t fdio_socket_stream_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = zxsio_wait_begin_stream,
    .wait_end = zxsio_wait_end_stream,
    .posix_ioctl = zxsio_posix_ioctl_stream,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvmsg = zxsio_recvmsg_stream,
    .sendmsg = zxsio_sendmsg_stream,
    .shutdown = fdio_socket_shutdown,
};

static fdio_ops_t fdio_socket_dgram_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = zxsio_wait_begin_dgram,
    .wait_end = zxsio_wait_end_dgram,
    .posix_ioctl = fdio_default_posix_ioctl,  // not supported
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvmsg = zxsio_recvmsg_dgram,
    .sendmsg = zxsio_sendmsg_dgram,
    .shutdown = fdio_socket_shutdown,
};

zx_status_t fdio_socket_create(fsocket::Control::SyncClient control, zx::socket socket,
                               fdio_t** out_io) {
  zx_info_socket_t info;
  zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  fdio_t* io = fdio_alloc(info.options & ZX_SOCKET_DATAGRAM ? &fdio_socket_dgram_ops
                                                            : &fdio_socket_stream_ops);
  if (io == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }
  status = zxio_socket_init(fdio_get_zxio_storage(io), std::move(control), std::move(socket), info);
  if (status != ZX_OK) {
    return status;
  }
  *out_io = io;
  return ZX_OK;
}

bool fdio_is_socket(fdio_t* io) {
  if (!io) {
    return false;
  }
  const fdio_ops_t* ops = fdio_get_ops(io);
  return ops == &fdio_socket_dgram_ops || ops == &fdio_socket_stream_ops;
}

fdio_t* fd_to_socket(int fd, zxio_socket_t** out_socket) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    *out_socket = nullptr;
    return nullptr;
  }

  if (fdio_is_socket(io)) {
    *out_socket = fdio_get_zxio_socket(io);
    return io;
  }

  fdio_release(io);
  *out_socket = nullptr;
  return nullptr;
}
