// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

namespace machina {

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::Stream<F>::WaitOnQueue(VirtioVsock* vsock) {
  zx_status_t status = waiter_.Wait(fbl::BindMember(vsock, F));
  return status == ZX_ERR_ALREADY_BOUND ? ZX_OK : status;
}

VirtioVsock::VirtioVsock(const PhysMem& phys_mem,
                         async_t* async,
                         uint32_t guest_cid)
    : VirtioDeviceBase(phys_mem),
      async_(async),
      rx_stream_(async, rx_queue()),
      tx_stream_(async, tx_queue()) {
  FXL_DCHECK(guest_cid > kVirtioVsockHostCid || guest_cid < UINT32_MAX)
      << "CID is within reserved range";
  config_.guest_cid = guest_cid;
}

uint32_t VirtioVsock::guest_cid() const {
  fbl::AutoLock lock(&config_mutex_);
  return config_.guest_cid;
}

bool VirtioVsock::HasConnection(uint32_t src_cid,
                                uint32_t src_port,
                                uint32_t dst_port) const {
  ConnectionKey key{src_cid, src_port, dst_port};
  fbl::AutoLock lock(&mutex_);
  return connections_.find(key) != connections_.end();
}

zx_status_t VirtioVsock::Connect(uint32_t src_cid,
                                 uint32_t src_port,
                                 uint32_t dst_port,
                                 zx::socket socket) {
  ConnectionKey key{src_cid, src_port, dst_port};
  auto conn = fbl::make_unique<Connection>(async_);
  conn->op = VIRTIO_VSOCK_OP_REQUEST;
  conn->socket = std::move(socket);
  zx_status_t status = SetupConnection(key, conn.get());
  if (status != ZX_OK) {
    return status;
  }

  fbl::AutoLock lock(&mutex_);
  return AddConnectionLocked(key, std::move(conn));
}

zx_status_t VirtioVsock::Listen(uint32_t cid,
                                uint32_t port,
                                ConnectionAcceptor acceptor) {
  ListenerKey key{cid, port};
  fbl::AutoLock lock(&mutex_);
  bool inserted;
  std::tie(std::ignore, inserted) =
      listeners_.emplace(key, std::move(acceptor));
  if (!inserted) {
    FXL_LOG(ERROR) << "Listener already exists";
    return ZX_ERR_ALREADY_EXISTS;
  }
  zx_status_t status = tx_stream_.WaitOnQueue(this);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
    return status;
  }
  return ZX_OK;
}

zx_status_t VirtioVsock::SetupConnection(ConnectionKey key, Connection* conn) {
  conn->rx_wait.set_object(conn->socket.get());
  conn->rx_wait.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED |
                            ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);
  conn->rx_wait.set_handler([this, key](async_t* async, zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    return OnSocketReady(async, status, signal, key);
  });
  // We require a separate waiter due to the way zx_object_wait_async works with
  // ZX_WAIT_ASYNC_ONCE. If the socket was just created, it's transmit buffer
  // would be empty, and therefore ZX_SOCKET_WRITABLE would be asserted. When
  // invoke zx_object_wait_async, it will see that this signal is asserted and
  // create a port packet immediately and stop listening for further signals.
  // This masks our ability to listen for any other signals, therefore we split
  // waiting on ZX_SOCKET_WRITABLE.
  conn->tx_wait.set_object(conn->socket.get());
  conn->tx_wait.set_trigger(ZX_SOCKET_WRITABLE);
  conn->tx_wait.set_handler([this, key](async_t* async, zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    return OnSocketReady(async, status, signal, key);
  });

  zx_status_t status = conn->rx_wait.Begin();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    return status;
  }
  return ZX_OK;
}

zx_status_t VirtioVsock::AddConnectionLocked(ConnectionKey key,
                                             fbl::unique_ptr<Connection> conn) {
  bool inserted;
  std::tie(std::ignore, inserted) = connections_.emplace(key, std::move(conn));
  if (!inserted) {
    FXL_LOG(ERROR) << "Connection already exists";
    return ZX_ERR_ALREADY_EXISTS;
  }
  zx_status_t status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
    return status;
  }
  return ZX_OK;
}

VirtioVsock::Connection* VirtioVsock::GetConnectionLocked(ConnectionKey key) {
  auto it = connections_.find(key);
  return it == connections_.end() ? nullptr : it->second.get();
}

VirtioVsock::ConnectionAcceptor* VirtioVsock::GetAcceptorLocked(
    ListenerKey key) {
  auto it = listeners_.find(key);
  return it == listeners_.end() ? nullptr : &it->second;
}

virtio_vsock_hdr_t* VirtioVsock::GetHeaderLocked(VirtioQueue* queue,
                                                 uint16_t index,
                                                 virtio_desc_t* desc,
                                                 bool writable) {
  zx_status_t status = queue->ReadDesc(index, desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor from queue " << status;
    return nullptr;
  }
  if (desc->writable != writable) {
    FXL_LOG(ERROR) << "Descriptor is not "
                   << (writable ? "writable" : "readable");
    return nullptr;
  }
  if (desc->len < sizeof(virtio_vsock_hdr_t)) {
    FXL_LOG(ERROR) << "Descriptor is too small";
    return nullptr;
  }
  return static_cast<virtio_vsock_hdr_t*>(desc->addr);
}

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::WaitOnQueueLocked(ConnectionKey key,
                                           ConnectionSet* keys,
                                           Stream<F>* stream) {
  keys->insert(key);
  return stream->WaitOnQueue(this);
}

void VirtioVsock::WaitOnSocketLocked(zx_status_t status,
                                     ConnectionKey key,
                                     async::AutoWait* wait) {
  if (status == ZX_OK && !wait->is_pending()) {
    status = wait->Begin();
  }
  if (status != ZX_OK) {
    if (status != ZX_ERR_STOP) {
      FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    }
    if (status != ZX_ERR_ALREADY_EXISTS) {
      wait->Cancel();
      connections_.erase(key);
    }
  }
}

async_wait_result_t VirtioVsock::OnSocketReady(async_t* async,
                                               zx_status_t status,
                                               const zx_packet_signal_t* signal,
                                               ConnectionKey key) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on socket " << status;
    return ASYNC_WAIT_FINISHED;
  }

  fbl::AutoLock lock(&mutex_);

  // If the socket has been partially or fully closed, wait on the Virtio
  // receive queue.
  if (signal->observed & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED |
                          ZX_SOCKET_WRITE_DISABLED)) {
    Connection* conn = GetConnectionLocked(key);
    if (conn == nullptr) {
      FXL_LOG(ERROR) << "Socket does not exist";
      return ASYNC_WAIT_FINISHED;
    }

    zx_signals_t signals = conn->rx_wait.trigger();
    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      // The peer closed the socket, therefore we move to sending a full
      // connection shutdown.
      conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
      conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_PEER_CLOSED);
    } else {
      if (signal->observed & ZX_SOCKET_READ_DISABLED &&
          !(conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV)) {
        // The peer disabled reading, therefore we move to sending a partial
        // connection shutdown.
        conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
        conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV;
        conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_READ_DISABLED);
      }
      if (signal->observed & ZX_SOCKET_WRITE_DISABLED &&
          !(conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND)) {
        // The peer disabled writing, therefore we move to sending a partial
        // connection shutdown.
        conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
        conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND;
        conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_WRITE_DISABLED);
      }
    }
    status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
  }

  // If the socket is readable, wait on the Virtio receive queue.
  if (signal->observed & ZX_SOCKET_READABLE) {
    status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
  }

  // If the socket is writable, wait on the Virtio transmit queue.
  if (signal->observed & ZX_SOCKET_WRITABLE) {
    status = WaitOnQueueLocked(key, &writable_, &tx_stream_);
  }

  return status == ZX_OK ? ASYNC_WAIT_FINISHED : ASYNC_WAIT_AGAIN;
}

static zx_status_t set_credit(virtio_vsock_hdr_t* header,
                              const zx::socket& socket) {
  size_t max = 0;
  zx_status_t status =
      socket.get_property(ZX_PROP_SOCKET_TX_BUF_MAX, &max, sizeof(max));
  if (status != ZX_OK) {
    return status;
  }
  size_t used = 0;
  status = socket.get_property(ZX_PROP_SOCKET_TX_BUF_SIZE, &used, sizeof(used));
  if (status != ZX_OK) {
    return status;
  }
  header->buf_alloc = max;
  header->fwd_cnt = used;
  return ZX_OK;
}

void VirtioVsock::Mux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  uint32_t remote_cid = guest_cid();
  virtio_desc_t desc;
  fbl::AutoLock lock(&mutex_);
  for (auto i = readable_.begin(), end = readable_.end(); i != end;
       i = readable_.erase(i)) {
    Connection* conn = GetConnectionLocked(*i);
    if (conn == nullptr) {
      continue;
    }
    auto header = GetHeaderLocked(rx_queue(), index, &desc, true);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from read queue";
      return;
    }
    *header = {
        .src_cid = i->local_cid,
        .src_port = i->local_port,
        .dst_cid = remote_cid,
        .dst_port = i->remote_port,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = conn->op,
    };
    uint32_t used = sizeof(*header);

    // If reading was shutdown, but we're still receiving a read request, send
    // a connection reset.
    if (conn->op == VIRTIO_VSOCK_OP_RW &&
        conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
      conn->op = VIRTIO_VSOCK_OP_RST;
      FXL_LOG(ERROR) << "Receive was shutdown";
    }

    switch (conn->op) {
      case VIRTIO_VSOCK_OP_REQUEST:
        // We are sending a connection request, therefore we move to waiting
        // for response.
        status = WaitOnQueueLocked(*i, &writable_, &tx_stream_);
        if (status == ZX_OK) {
          status = set_credit(header, conn->socket);
        }
        conn->op = VIRTIO_VSOCK_OP_RESPONSE;
        break;
      case VIRTIO_VSOCK_OP_RESPONSE:
        // We are sending a connection response, therefore we move to ready to
        // read/write.
        WaitOnSocketLocked(status, *i, &conn->tx_wait);
        // fallthrough
      case VIRTIO_VSOCK_OP_CREDIT_UPDATE: {
        // We are sending a credit update, therefore we move to ready to
        // read/write.
        status = set_credit(header, conn->socket);
        conn->op = VIRTIO_VSOCK_OP_RW;
        break;
      }
      case VIRTIO_VSOCK_OP_RW:
        // We are reading from the socket.
        desc.addr = header + 1;
        desc.len -= used;
        do {
          size_t actual;
          status = conn->socket.read(0, desc.addr, desc.len, &actual);
          used += actual;
          if (!desc.has_next || actual < desc.len) {
            break;
          }
          status = rx_queue()->ReadDesc(desc.next, &desc);
        } while (status == ZX_OK);
        header->len = used - sizeof(*header);
        break;
      case VIRTIO_VSOCK_OP_SHUTDOWN:
        header->flags = conn->flags;
        if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
          // We are sending a full connection shutdown, therefore we move to
          // waiting for a connection reset.
          conn->op = VIRTIO_VSOCK_OP_RST;
        } else {
          // One side of the connection is still active, therefore we move to
          // ready to read/write.
          conn->op = VIRTIO_VSOCK_OP_RW;
        }
        status = WaitOnQueueLocked(*i, &writable_, &tx_stream_);
        break;
      default:
      case VIRTIO_VSOCK_OP_RST:
        // We are sending a connection reset, therefore remove the connection.
        header->op = VIRTIO_VSOCK_OP_RST;
        status = ZX_ERR_STOP;
        break;
    }

    WaitOnSocketLocked(status, *i, &conn->rx_wait);
    rx_queue()->Return(index, used);
    status = rx_queue()->NextAvail(&index);
    if (status != ZX_OK) {
      readable_.erase(i);
      break;
    }
  }
}

static void set_shutdown(virtio_vsock_hdr_t* header) {
  header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
  header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
}

static uint32_t shutdown_flags(uint32_t f) {
  return (f & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV ? ZX_SOCKET_SHUTDOWN_READ : 0) |
         (f & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND ? ZX_SOCKET_SHUTDOWN_WRITE : 0);
}

void VirtioVsock::Demux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  virtio_desc_t desc;
  fbl::AutoLock lock(&mutex_);
  do {
    auto header = GetHeaderLocked(tx_queue(), index, &desc, false);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from write queue";
      return;
    } else if (header->type != VIRTIO_VSOCK_TYPE_STREAM) {
      set_shutdown(header);
      FXL_LOG(ERROR) << "Only stream sockets are supported";
    }
    uint32_t used = sizeof(*header);
    ConnectionKey key{
        static_cast<uint32_t>(header->dst_cid),
        header->dst_port,
        header->src_port,
    };

    Connection* conn = nullptr;
    ConnectionAcceptor* acceptor = nullptr;
    switch (header->op) {
      case VIRTIO_VSOCK_OP_REQUEST:
        // We received a connection request, therefore we lookup the acceptor.
        acceptor = GetAcceptorLocked(key.listener_key());
        if (acceptor == nullptr) {
          // Build a connection to send a connection reset.
          auto new_conn = fbl::make_unique<Connection>(async_);
          conn = new_conn.get();
          status = AddConnectionLocked(key, std::move(new_conn));
          set_shutdown(header);
          FXL_LOG(ERROR) << "Listener does not exist";
        }
        break;
      default:
        // For any other operation, we lookup the existing connection.
        conn = GetConnectionLocked(key);
        if (conn == nullptr) {
          // Build a connection to send a connection reset.
          auto new_conn = fbl::make_unique<Connection>(async_);
          conn = new_conn.get();
          status = AddConnectionLocked(key, std::move(new_conn));
          set_shutdown(header);
          FXL_LOG(ERROR) << "Connection does not exist";
        } else if (writable_.find(key) == writable_.end()) {
          // There was a write, but the socket is not ready.
          continue;
        } else if (conn->op == VIRTIO_VSOCK_OP_RW &&
                   conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND) {
          // We are receiving a write, but send was shutdown.
          set_shutdown(header);
          FXL_LOG(ERROR) << "Send was shutdown";
        }
        break;
    }

    switch (header->op) {
      case VIRTIO_VSOCK_OP_REQUEST: {
        // We received a connection request.
        auto new_conn = fbl::make_unique<Connection>(async_);
        conn = new_conn.get();
        new_conn->op = VIRTIO_VSOCK_OP_RESPONSE;
        status = (*acceptor)(&new_conn->socket);
        if (status != ZX_OK) {
          new_conn->op = VIRTIO_VSOCK_OP_RST;
        } else {
          status = SetupConnection(key, new_conn.get());
          if (status != ZX_OK) {
            break;
          }
        }
        status = AddConnectionLocked(key, std::move(new_conn));
        break;
      }
      case VIRTIO_VSOCK_OP_RESPONSE:
        // We received a connection response, therefore we move to ready to
        // read/write.
        conn->op = VIRTIO_VSOCK_OP_RW;
        WaitOnSocketLocked(status, key, &conn->rx_wait);
        break;
      case VIRTIO_VSOCK_OP_RW:
        // We are writing to the socket.
        desc.addr = header + 1;
        desc.len -= used;
        do {
          uint32_t len = std::min(desc.len, header->len);
          size_t actual;
          status = conn->socket.write(0, desc.addr, len, &actual);
          used += actual;
          header->len -= actual;
          if (!desc.has_next || header->len == 0) {
            break;
          }
          status = tx_queue()->ReadDesc(desc.next, &desc);
        } while (status == ZX_OK);
        break;
      case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        // We received a credit update, which we ignore.
        break;
      case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        // We received a credit request, therefore we move to sending a credit
        // update.
        conn->op = VIRTIO_VSOCK_OP_CREDIT_UPDATE;
        status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
        break;
      case VIRTIO_VSOCK_OP_RST:
        // We received a connection reset, therefore remove the connection.
        status = ZX_ERR_STOP;
        break;
      default:
        header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      case VIRTIO_VSOCK_OP_SHUTDOWN:
        // We received a full connection shutdown, therefore we move to sending
        // a connection reset.
        if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
          conn->op = VIRTIO_VSOCK_OP_RST;
          status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
        } else if (header->flags != 0) {
          uint32_t flags = shutdown_flags(header->flags);
          status = conn->socket.write(flags, nullptr, 0, nullptr);
        }
        break;
    }

    writable_.erase(key);
    if (conn != nullptr && conn->socket.is_valid()) {
      WaitOnSocketLocked(status, key, &conn->tx_wait);
    }
    tx_queue()->Return(index, used);
  } while (tx_queue()->NextAvail(&index) == ZX_OK);

  // If there are listeners, wait on the transmit queue.
  if (!listeners_.empty()) {
    zx_status_t status = tx_stream_.WaitOnQueue(this);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait on queue " << status;
    }
  }
}

}  // namespace machina
