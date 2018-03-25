// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

namespace machina {

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::Stream<F>::WaitOnQueue(VirtioVsock* vsock) {
  zx_status_t status = queue_wait_.Wait(fbl::BindMember(vsock, F));
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

bool VirtioVsock::HasConnection(uint32_t cid, uint32_t port) const {
  Key key{cid, port};
  fbl::AutoLock lock(&mutex_);
  return connections_.find(key) != connections_.end();
}

zx_status_t VirtioVsock::Connect(uint32_t src_cid,
                                 uint32_t src_port,
                                 uint32_t dst_port,
                                 zx::socket socket) {
  uint32_t dst_cid;
  {
    fbl::AutoLock lock(&config_mutex_);
    dst_cid = config_.guest_cid;
  }

  Key key{src_cid, src_port};
  auto conn = fbl::make_unique<Connection>();
  conn->dst_cid = dst_cid;
  conn->dst_port = dst_port;
  conn->socket = std::move(socket);
  conn->rx_wait.set_object(conn->socket.get());
  conn->tx_wait.set_object(conn->socket.get());
  conn->op = VIRTIO_VSOCK_OP_REQUEST;

  fbl::AutoLock lock(&mutex_);
  return AddConnectionLocked(key, std::move(conn));
}

zx_status_t VirtioVsock::Listen(uint32_t cid,
                                uint32_t port,
                                ConnectionAcceptor acceptor) {
  Key key{cid, port};
  auto conn = fbl::make_unique<Connection>();
  conn->acceptor = std::move(acceptor);
  conn->op = VIRTIO_VSOCK_OP_RESPONSE;

  fbl::AutoLock lock(&mutex_);
  return AddConnectionLocked(key, std::move(conn));
}

zx_status_t VirtioVsock::AddConnectionLocked(Key key,
                                             fbl::unique_ptr<Connection> conn) {
  conn->rx_wait.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED |
                            ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);
  conn->rx_wait.set_handler([this, key](async_t* async, zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    return OnSocketReady(async, status, signal, key);
  });
  conn->tx_wait.set_trigger(ZX_SOCKET_WRITABLE);
  conn->tx_wait.set_handler([this, key](async_t* async, zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    return OnSocketReady(async, status, signal, key);
  });

  if (conn->acceptor) {
    zx_status_t status = WaitOnQueueLocked(key, &writable_, &tx_stream_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait on queue " << status;
      return status;
    }
  } else if (conn->socket.is_valid()) {
    zx_status_t status = conn->rx_wait.Begin(async_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait on socket " << status;
      return status;
    }
    status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait on queue " << status;
      return status;
    }
  }

  bool inserted;
  std::tie(std::ignore, inserted) = connections_.emplace(key, std::move(conn));
  return inserted ? ZX_OK : ZX_ERR_ALREADY_EXISTS;
}

VirtioVsock::Connection* VirtioVsock::GetConnectionLocked(Key key) {
  auto it = connections_.find(key);
  return it == connections_.end() ? nullptr : it->second.get();
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
  if (desc->has_next) {
    FXL_LOG(ERROR) << "Packet data must be on a single buffer";
    return nullptr;
  }
  return reinterpret_cast<virtio_vsock_hdr_t*>(desc->addr);
}

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::WaitOnQueueLocked(Key key,
                                           KeySet* keys,
                                           Stream<F>* stream) {
  keys->insert(key);
  return stream->WaitOnQueue(this);
}

void VirtioVsock::WaitOnSocketLocked(zx_status_t status,
                                     Key key,
                                     async::Wait* wait) {
  if (status == ZX_OK) {
    status = wait->Begin(async_);
  }
  if (status != ZX_OK) {
    if (status != ZX_ERR_STOP) {
      FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    }
    if (status != ZX_ERR_ALREADY_EXISTS) {
      wait->Cancel(async_);
      connections_.erase(key);
    }
  }
}

async_wait_result_t VirtioVsock::OnSocketReady(async_t* async,
                                               zx_status_t status,
                                               const zx_packet_signal_t* signal,
                                               Key key) {
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
      conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_PEER_CLOSED);
      conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
      conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
    } else {
      if (signal->observed & ZX_SOCKET_READ_DISABLED &&
          !(conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV)) {
        // The peer disabled reading, therefore we move to sending a partial
        // connection shutdown.
        conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_READ_DISABLED);
        conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
        conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV;
      }
      if (signal->observed & ZX_SOCKET_WRITE_DISABLED &&
          !(conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND)) {
        // The peer disabled writing, therefore we move to sending a partial
        // connection shutdown.
        conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_WRITE_DISABLED);
        conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
        conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND;
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

void VirtioVsock::Mux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

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
        .src_cid = i->cid,
        .src_port = i->port,
        .dst_cid = conn->dst_cid,
        .dst_port = conn->dst_port,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = conn->op,
    };
    uint32_t used = sizeof(*header);

    // If reading was shutdown, but we're still receiving a read request, send
    // a connection reset.
    if (conn->op == VIRTIO_VSOCK_OP_RW &&
        conn->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
      conn->op = VIRTIO_VSOCK_OP_RST;
    }

    switch (conn->op) {
      case VIRTIO_VSOCK_OP_REQUEST:
        // We are sending a connection request, therefore we move to a waiting
        // for response state.
        conn->op = VIRTIO_VSOCK_OP_RESPONSE;
        status = WaitOnQueueLocked(*i, &writable_, &tx_stream_);
        break;
      case VIRTIO_VSOCK_OP_RESPONSE:
        // We are sending a connection response, therefore we move to a ready to
        // read/write state.
        conn->op = VIRTIO_VSOCK_OP_RW;
        WaitOnSocketLocked(status, *i, &conn->tx_wait);
        break;
      case VIRTIO_VSOCK_OP_RW: {
        // We are reading from the socket.
        size_t actual;
        status = conn->socket.read(0, header + 1, desc.len - used, &actual);
        used += static_cast<uint32_t>(actual);
        break;
      }
      case VIRTIO_VSOCK_OP_SHUTDOWN:
        header->flags = conn->flags;
        if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
          // We are sending a full connection shutdown, therefore we move to a
          // waiting for a connection reset state.
          conn->op = VIRTIO_VSOCK_OP_RST;
        } else {
          // One side of the connection is still active, therefore we move to a
          // ready to read/write state.
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

uint32_t zx_shutdown_flags(uint32_t f) {
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
      header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
      header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      FXL_LOG(ERROR) << "Only stream sockets are supported";
    }
    uint32_t used = sizeof(*header);
    Key key{static_cast<uint32_t>(header->dst_cid), header->dst_port};
    Connection* conn = GetConnectionLocked(key);
    if (conn == nullptr) {
      // Build a connection to send a connection reset.
      auto new_conn = fbl::make_unique<Connection>();
      new_conn->dst_cid = header->src_cid;
      new_conn->dst_port = header->src_port;
      new_conn->op = VIRTIO_VSOCK_OP_RESPONSE;

      conn = new_conn.get();
      status = AddConnectionLocked(key, std::move(new_conn));
      header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
      header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
    } else if (writable_.find(key) == writable_.end()) {
      // There was a write, but the socket is not ready.
      continue;
    }

    switch (header->op) {
      case VIRTIO_VSOCK_OP_REQUEST:
        // We received a connection request.
        if (conn->op == VIRTIO_VSOCK_OP_RESPONSE && conn->acceptor) {
          // We were expecting a connection request, therefore create a new
          // connection we move it to a sending a response state.
          auto new_conn = fbl::make_unique<Connection>();
          std::tie(key.port, new_conn->socket) = conn->acceptor();
          new_conn->dst_cid = header->src_cid;
          new_conn->dst_port = header->src_port;
          new_conn->rx_wait.set_object(new_conn->socket.get());
          new_conn->tx_wait.set_object(new_conn->socket.get());
          new_conn->op = VIRTIO_VSOCK_OP_RESPONSE;

          conn = new_conn.get();
          status = AddConnectionLocked(key, std::move(new_conn));
        } else {
          // We were not expecting a connection request, therefore we move to a
          // sending a reset state.
          conn->op = VIRTIO_VSOCK_OP_RST;
        }
        if (status == ZX_OK) {
          status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
        }
        break;
      case VIRTIO_VSOCK_OP_RESPONSE:
        // We received a connection response, therefore we move to a ready to
        // read/write state.
        conn->dst_cid = header->src_cid;
        conn->dst_port = header->src_port;
        conn->op = VIRTIO_VSOCK_OP_RW;
        WaitOnSocketLocked(status, key, &conn->rx_wait);
        break;
      case VIRTIO_VSOCK_OP_RW: {
        // We are writing to the socket.
        size_t actual;
        status = conn->socket.write(0, header + 1, desc.len - used, &actual);
        used += static_cast<uint32_t>(actual);
        break;
      }
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
        }
        if (header->flags != 0 && status == ZX_OK) {
          uint32_t flags = zx_shutdown_flags(header->flags);
          status = conn->socket.write(flags, nullptr, 0, nullptr);
        }
        break;
    }

    // When handling a listen socket, we always want to keep it in the writable
    // set for subsequent connections.
    if (conn->op != VIRTIO_VSOCK_OP_RESPONSE) {
      writable_.erase(key);
    }
    if (conn->socket.is_valid()) {
      WaitOnSocketLocked(status, key, &conn->tx_wait);
    }
    tx_queue()->Return(index, used);
  } while (tx_queue()->NextAvail(&index) == ZX_OK);
}

zx_status_t EphemeralPortAllocator::Init() {
  return bitmap_.Reset(kNumEphemeralPorts);
}

zx_status_t EphemeralPortAllocator::Alloc(uint32_t* port) {
  size_t value;
  {
    fbl::AutoLock lock(&mutex_);
    if (bitmap_.Scan(0, kNumEphemeralPorts, true, &value)) {
      return ZX_ERR_NOT_FOUND;
    }
    bitmap_.SetOne(value);
  }
  *port = value + kEphemeralPortBegin;
  return ZX_OK;
}

void EphemeralPortAllocator::Free(uint32_t port) {
  fbl::AutoLock lock(&mutex_);
  bitmap_.ClearOne(port - kEphemeralPortBegin);
}

}  // namespace machina
