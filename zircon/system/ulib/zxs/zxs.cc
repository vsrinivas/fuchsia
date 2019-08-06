// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/ioctl.h>
#include <zircon/syscalls.h>

#include <memory>

zx_status_t zxs_close(zxs_socket_t socket) {
  zx_status_t status;
  zx_status_t io_status = socket.control.Close_Deprecated(&status);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

static zx_status_t zxs_write(const zxs_socket_t* socket, const void* buffer, size_t capacity,
                             size_t* out_actual) {
  return socket->socket.write(0, buffer, capacity, out_actual);
}

static zx_status_t zxs_read(const zxs_socket_t* socket, void* buffer, size_t capacity,
                            size_t* out_actual) {
  zx_status_t status = socket->socket.read(0, buffer, capacity, out_actual);
  if (status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE) {
    *out_actual = 0u;
    return ZX_OK;
  }
  return status;
}

static zx_status_t zxs_sendmsg_stream(const zxs_socket_t* socket, const struct msghdr* msg,
                                      size_t* out_actual) {
  size_t total = 0u;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];
    if (iov->iov_len <= 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    size_t actual = 0u;
    zx_status_t status = zxs_write(socket, iov->iov_base, iov->iov_len, &actual);
    if (status != ZX_OK) {
      if (total > 0) {
        break;
      }
      return status;
    }
    total += actual;
    if (actual != iov->iov_len) {
      break;
    }
  }
  *out_actual = total;
  return ZX_OK;
}

static zx_status_t zxs_sendmsg_dgram(const zxs_socket_t* socket, const struct msghdr* msg,
                                     size_t* out_actual) {
  if (msg->msg_namelen > sizeof((fdio_socket_msg_t*)nullptr)->addr) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t total = 0u;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];
    if (iov->iov_len <= 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    total += iov->iov_len;
  }
  size_t encoded_size = total + FDIO_SOCKET_MSG_HEADER_SIZE;

  std::unique_ptr<uint8_t[]> buf(new uint8_t[encoded_size]);
  fdio_socket_msg_t* m = reinterpret_cast<fdio_socket_msg_t*>(buf.get());
  if (msg->msg_name != nullptr) {
    memcpy(&m->addr, msg->msg_name, msg->msg_namelen);
  }
  m->addrlen = msg->msg_namelen;
  m->flags = 0;
  char* data = m->data;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];
    memcpy(data, iov->iov_base, iov->iov_len);
    data += iov->iov_len;
  }
  size_t actual = 0u;
  zx_status_t status = zxs_write(socket, m, encoded_size, &actual);
  if (status == ZX_OK) {
    *out_actual = total;
  }
  return status;
}

static zx_status_t zxs_recvmsg_stream(const zxs_socket_t* socket, struct msghdr* msg,
                                      size_t* out_actual) {
  size_t total = 0u;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];
    size_t actual = 0u;
    zx_status_t status = zxs_read(socket, iov->iov_base, iov->iov_len, &actual);
    if (status != ZX_OK) {
      if (total > 0) {
        break;
      }
      return status;
    }
    total += actual;
    if (actual != iov->iov_len) {
      break;
    }
  }
  *out_actual = total;
  return ZX_OK;
}

static zx_status_t zxs_recvmsg_dgram(const zxs_socket_t* socket, struct msghdr* msg,
                                     size_t* out_actual) {
  // Read 1 extra byte to detect if the buffer is too small to fit the whole
  // packet, so we can set MSG_TRUNC flag if necessary.
  size_t encoded_size = FDIO_SOCKET_MSG_HEADER_SIZE + 1;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];
    if (iov->iov_len <= 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    encoded_size += iov->iov_len;
  }

  std::unique_ptr<uint8_t[]> buf(new uint8_t[encoded_size]);
  fdio_socket_msg_t* m = reinterpret_cast<fdio_socket_msg_t*>(buf.get());
  size_t actual = 0u;
  zx_status_t status = zxs_read(socket, m, encoded_size, &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual < FDIO_SOCKET_MSG_HEADER_SIZE) {
    return ZX_ERR_INTERNAL;
  }
  actual -= FDIO_SOCKET_MSG_HEADER_SIZE;
  if (msg->msg_name != nullptr) {
    int bytes_to_copy = (msg->msg_namelen < m->addrlen) ? msg->msg_namelen : m->addrlen;
    memcpy(msg->msg_name, &m->addr, bytes_to_copy);
  }
  msg->msg_namelen = m->addrlen;
  msg->msg_flags = m->flags;
  char* data = m->data;
  size_t remaining = actual;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];
    if (remaining == 0) {
      iov->iov_len = 0;
    } else {
      if (remaining < iov->iov_len)
        iov->iov_len = remaining;
      memcpy(iov->iov_base, data, iov->iov_len);
      data += iov->iov_len;
      remaining -= iov->iov_len;
    }
  }

  if (remaining > 0) {
    msg->msg_flags |= MSG_TRUNC;
    actual -= remaining;
  }

  *out_actual = actual;
  return ZX_OK;
}

zx_status_t zxs_send(const zxs_socket_t* socket, const void* buffer, size_t capacity,
                     size_t* out_actual) {
  if (socket->flags & ZXS_FLAG_DATAGRAM) {
    struct iovec iov;
    iov.iov_base = const_cast<void*>(buffer);
    iov.iov_len = capacity;

    struct msghdr msg;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    return zxs_sendmsg_dgram(socket, &msg, out_actual);
  } else {
    return zxs_write(socket, buffer, capacity, out_actual);
  }
}

zx_status_t zxs_recv(const zxs_socket_t* socket, void* buffer, size_t capacity,
                     size_t* out_actual) {
  if (socket->flags & ZXS_FLAG_DATAGRAM) {
    struct iovec iov;
    iov.iov_base = buffer;
    iov.iov_len = capacity;

    struct msghdr msg;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    return zxs_recvmsg_dgram(socket, &msg, out_actual);
  } else {
    return zxs_read(socket, buffer, capacity, out_actual);
  }
}

zx_status_t zxs_sendto(const zxs_socket_t* socket, const struct sockaddr* addr, size_t addr_length,
                       const void* buffer, size_t capacity, size_t* out_actual) {
  struct iovec iov;
  iov.iov_base = const_cast<void*>(buffer);
  iov.iov_len = capacity;

  struct msghdr msg;
  msg.msg_name = const_cast<struct sockaddr*>(addr);
  msg.msg_namelen = static_cast<socklen_t>(addr_length);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;  // this field is ignored

  return zxs_sendmsg(socket, &msg, out_actual);
}

zx_status_t zxs_recvfrom(const zxs_socket_t* socket, struct sockaddr* addr, size_t addr_capacity,
                         size_t* out_addr_actual, void* buffer, size_t capacity,
                         size_t* out_actual) {
  struct iovec iov;
  iov.iov_base = buffer;
  iov.iov_len = capacity;

  struct msghdr msg;
  msg.msg_name = addr;
  msg.msg_namelen = static_cast<socklen_t>(addr_capacity);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  zx_status_t status = zxs_recvmsg(socket, &msg, out_actual);
  *out_addr_actual = msg.msg_namelen;
  return status;
}

zx_status_t zxs_sendmsg(const zxs_socket_t* socket, const struct msghdr* msg, size_t* out_actual) {
  if (socket->flags & ZXS_FLAG_DATAGRAM) {
    return zxs_sendmsg_dgram(socket, msg, out_actual);
  } else {
    return zxs_sendmsg_stream(socket, msg, out_actual);
  }
}
zx_status_t zxs_recvmsg(const zxs_socket_t* socket, struct msghdr* msg, size_t* out_actual) {
  if (socket->flags & ZXS_FLAG_DATAGRAM) {
    return zxs_recvmsg_dgram(socket, msg, out_actual);
  } else {
    return zxs_recvmsg_stream(socket, msg, out_actual);
  }
}
