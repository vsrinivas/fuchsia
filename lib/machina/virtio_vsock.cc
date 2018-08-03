// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

#include <fbl/auto_call.h>
#include <lib/fsl/handles/object_info.h>
#include <zircon/types.h>

namespace machina {

template <VirtioVsock::StreamFunc F>
VirtioVsock::Stream<F>::Stream(async_dispatcher_t* dispatcher,
                               VirtioQueue* queue, VirtioVsock* vsock)
    : waiter_(dispatcher, queue, fit::bind_member(vsock, F)) {}

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::Stream<F>::WaitOnQueue() {
  return waiter_.Begin();
}

// We take a |queue_callback| to decouple the connection from the device. This
// allows a connection to wait on a Virtio queue and update the device state,
// without having direct access to the device.
VirtioVsock::Connection::Connection(
    async_dispatcher_t* dispatcher,
    fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
    fit::closure queue_callback)
    : dispatcher_(dispatcher),
      accept_callback_(std::move(accept_callback)),
      queue_callback_(std::move(queue_callback)) {}

VirtioVsock::Connection::~Connection() {
  if (accept_callback_) {
    accept_callback_(ZX_ERR_CONNECTION_REFUSED);
  }
}

zx_status_t VirtioVsock::Connection::Accept() {
  // The guest has accepted the connection request. Move the connection into the
  // RW state and let the connector know that the connection is ready.
  //
  // If we don't have an acceptor then this is a spurious response so reset the
  // connection.
  if (accept_callback_) {
    UpdateOp(VIRTIO_VSOCK_OP_RW);
    accept_callback_(ZX_OK);
    accept_callback_ = nullptr;
    return WaitOnReceive(ZX_OK);
  } else {
    UpdateOp(VIRTIO_VSOCK_OP_RST);
    return ZX_OK;
  }
}

// Connection state machine:
//
//                          -------------       --------------
//                         |CREDIT_UPDATE|     |   ANY_STATE  |
//                          -------------       --------------
//                             /|\  |           |           |
//                              |   |           |           |
//                              |  \|/         \|/         \|/
//  -------      --------      -------       --------      -----
// |REQUEST|--->|RESPONSE|--->|   RW   |<---|SHUTDOWN|--->|RESET|
//  -------      --------      --------      --------      -----
//                              |  /|\
//                              |   |
//                             \|/  |
//                          -------------
//                         |CREDIT_REQUEST|
//                          -------------
void VirtioVsock::Connection::UpdateOp(uint16_t new_op) {
  fbl::AutoLock lock(&op_update_mutex_);

  if (new_op == op_) {
    return;
  }

  switch (new_op) {
    case VIRTIO_VSOCK_OP_SHUTDOWN:
    case VIRTIO_VSOCK_OP_RST:
      op_ = new_op;
      return;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      if (op_ == VIRTIO_VSOCK_OP_RW) {
        op_ = new_op;
        return;
      }
      break;
    case VIRTIO_VSOCK_OP_RW:
      switch (op_) {
        // SHUTDOWN -> RW only valid if one of the streams is still active.
        case VIRTIO_VSOCK_OP_SHUTDOWN:
          if (flags_ == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
            break;
          }
        case VIRTIO_VSOCK_OP_RESPONSE:
        case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
          op_ = new_op;
          return;
      }
      break;
    case VIRTIO_VSOCK_OP_RESPONSE:
      if (op_ == VIRTIO_VSOCK_OP_REQUEST) {
        op_ = new_op;
        return;
      }
      break;
    // No transitions to REQUEST allowed, but this is the initial state of the
    // connection object.
    case VIRTIO_VSOCK_OP_REQUEST:
    default:
      break;
  }
  FXL_LOG(ERROR) << "Invalid state transition from " << op_ << " to " << new_op
                 << "; resetting connection";
  op_ = VIRTIO_VSOCK_OP_RST;
}

uint32_t VirtioVsock::Connection::PeerFree() const {
  // See 5.7.6.3 Buffer Space Management, from the Virtio Socket Device spec.
  return peer_buf_alloc_ - (tx_cnt_ - peer_fwd_cnt_);
}

void VirtioVsock::Connection::ReadCredit(virtio_vsock_hdr_t* header) {
  peer_buf_alloc_ = header->buf_alloc;
  peer_fwd_cnt_ = header->fwd_cnt;
}

static zx_status_t wait(async_dispatcher_t* dispatcher, async::Wait* wait,
                        zx_status_t status) {
  if (status == ZX_ERR_SHOULD_WAIT) {
    status = ZX_OK;
  }
  if (status == ZX_OK) {
    if (wait->has_handler() && !wait->is_pending()) {
      status = wait->Begin(dispatcher);
    }
  }
  if (status != ZX_OK) {
    if (status != ZX_ERR_STOP) {
      FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    }
    if (status != ZX_ERR_ALREADY_EXISTS) {
      wait->Cancel();
    }
  }
  return status;
}

zx_status_t VirtioVsock::Connection::WaitOnTransmit(zx_status_t status) {
  return wait(dispatcher_, &tx_wait_, status);
}

zx_status_t VirtioVsock::Connection::WaitOnReceive(zx_status_t status) {
  return wait(dispatcher_, &rx_wait_, status);
}

VirtioVsock::SocketConnection::SocketConnection(
    zx::handle handle, async_dispatcher_t* dispatcher,
    fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
    fit::closure queue_callback)
    : Connection(dispatcher, std::move(accept_callback),
                 std::move(queue_callback)),
      socket_(std::move(handle)) {}

VirtioVsock::SocketConnection::~SocketConnection() {
  // We must cancel the async wait before the socket is destroyed.
  rx_wait_.Cancel();
  tx_wait_.Cancel();
}

zx_status_t VirtioVsock::SocketConnection::Init() {
  rx_wait_.set_object(socket_.get());
  rx_wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED |
                       ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);
  rx_wait_.set_handler(
      [this](async_dispatcher_t* dispatcher, async::Wait* wait,
             zx_status_t status,
             const zx_packet_signal_t* signal) { OnReady(status, signal); });

  tx_wait_.set_object(socket_.get());
  tx_wait_.set_trigger(ZX_SOCKET_WRITABLE);
  tx_wait_.set_handler(
      [this](async_dispatcher_t* dispatcher, async::Wait* wait,
             zx_status_t status,
             const zx_packet_signal_t* signal) { OnReady(status, signal); });

  return WaitOnReceive(ZX_OK);
}

void VirtioVsock::SocketConnection::OnReady(zx_status_t status,
                                            const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on socket " << status;
    return;
  }

  // If the socket has been partially or fully closed, wait on the Virtio
  // receive queue.
  if (signal->observed & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED |
                          ZX_SOCKET_WRITE_DISABLED)) {
    zx_signals_t signals = rx_wait_.trigger();
    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      // The peer closed the socket, therefore we move to sending a full
      // connection shutdown.
      UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
      flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      rx_wait_.set_trigger(signals & ~ZX_SOCKET_PEER_CLOSED);
    } else {
      if (signal->observed & ZX_SOCKET_READ_DISABLED &&
          !(flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV)) {
        // The peer disabled reading, therefore we move to sending a partial
        // connection shutdown.
        UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
        flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV;
        rx_wait_.set_trigger(signals & ~ZX_SOCKET_READ_DISABLED);
      }
      if (signal->observed & ZX_SOCKET_WRITE_DISABLED &&
          !(flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND)) {
        // The peer disabled writing, therefore we move to sending a partial
        // connection shutdown.
        UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
        flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND;
        rx_wait_.set_trigger(signals & ~ZX_SOCKET_WRITE_DISABLED);
      }
    }
    queue_callback_();
    return;
  }

  // If the socket is readable and our peer has buffer space, wait on the
  // Virtio receive queue.
  if (signal->observed & ZX_SOCKET_READABLE && PeerFree() > 0) {
    queue_callback_();
    return;
  }

  // If the socket is writable and we last reported the buffer as full, send a
  // credit update message to the guest indicating buffer space is now
  // available.
  if (reported_buf_avail_ == 0 && signal->observed & ZX_SOCKET_WRITABLE) {
    UpdateOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
    queue_callback_();
  }
}

zx_status_t VirtioVsock::SocketConnection::WriteCredit(
    virtio_vsock_hdr_t* header) {
  size_t max = 0;
  zx_status_t status =
      socket_.get_property(ZX_PROP_SOCKET_TX_BUF_MAX, &max, sizeof(max));
  if (status != ZX_OK) {
    return status;
  }
  size_t used = 0;
  status =
      socket_.get_property(ZX_PROP_SOCKET_TX_BUF_SIZE, &used, sizeof(used));
  if (status != ZX_OK) {
    return status;
  }

  header->buf_alloc = max;
  header->fwd_cnt = rx_cnt_ - used;
  reported_buf_avail_ = max - used;
  return reported_buf_avail_ != 0 ? ZX_OK : ZX_ERR_UNAVAILABLE;
}

zx_status_t VirtioVsock::SocketConnection::Shutdown(uint32_t flags) {
  uint32_t shutdown_flags =
      (flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV ? ZX_SOCKET_SHUTDOWN_READ : 0) |
      (flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND ? ZX_SOCKET_SHUTDOWN_WRITE : 0);
  return socket_.write(shutdown_flags, nullptr, 0, nullptr);
}

static zx_status_t setup_desc_chain(VirtioQueue* queue,
                                    virtio_vsock_hdr_t* header,
                                    virtio_desc_t* desc) {
  desc->addr = header + 1;
  desc->len -= sizeof(*header);
  // If the descriptor was only large enough for the header, read the next
  // descriptor, if there is one.
  if (desc->len == 0 && desc->has_next) {
    return queue->ReadDesc(desc->next, desc);
  }
  return ZX_OK;
}

zx_status_t VirtioVsock::SocketConnection::Read(VirtioQueue* queue,
                                                virtio_vsock_hdr_t* header,
                                                virtio_desc_t* desc,
                                                uint32_t* used) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    size_t len = std::min(desc->len, PeerFree());
    size_t actual;
    status = socket_.read(0, desc->addr, len, &actual);
    if (status != ZX_OK) {
      break;
    }

    *used += actual;
    tx_cnt_ += actual;
    if (PeerFree() == 0 || !desc->has_next || actual < desc->len) {
      break;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  header->len = *used;
  return status;
}

zx_status_t VirtioVsock::SocketConnection::Write(VirtioQueue* queue,
                                                 virtio_vsock_hdr_t* header,
                                                 virtio_desc_t* desc) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    uint32_t len = std::min(desc->len, header->len);
    size_t actual;
    status = socket_.write(0, desc->addr, len, &actual);
    rx_cnt_ += actual;
    header->len -= actual;
    if (status != ZX_OK || actual < len) {
      // If we've failed to write just reset the connection. Note that it
      // should not be possible to receive a ZX_ERR_SHOULD_WAIT here if
      // the guest is honoring our credit messages that describe socket
      // buffer space.
      FXL_LOG(ERROR) << "Failed to write to socket " << status;
      UpdateOp(VIRTIO_VSOCK_OP_RST);
      return ZX_OK;
    }

    reported_buf_avail_ -= actual;
    if (reported_buf_avail_ == 0 || !desc->has_next || header->len == 0) {
      return ZX_OK;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  return status;
}

VirtioVsock::ChannelConnection::ChannelConnection(
    zx::handle handle, async_dispatcher_t* dispatcher,
    fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
    fit::closure queue_callback)
    : Connection(dispatcher, std::move(accept_callback),
                 std::move(queue_callback)),
      channel_(std::move(handle)) {}

VirtioVsock::ChannelConnection::~ChannelConnection() {
  // We must cancel the async wait before the channel is destroyed.
  rx_wait_.Cancel();
  tx_wait_.Cancel();
}

zx_status_t VirtioVsock::ChannelConnection::Init() {
  rx_wait_.set_object(channel_.get());
  rx_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  rx_wait_.set_handler(
      [this](async_dispatcher_t* dispatcher, async::Wait* wait,
             zx_status_t status,
             const zx_packet_signal_t* signal) { OnReady(status, signal); });

  tx_wait_.set_object(channel_.get());
  tx_wait_.set_trigger(ZX_CHANNEL_WRITABLE);
  tx_wait_.set_handler(
      [this](async_dispatcher_t* dispatcher, async::Wait* wait,
             zx_status_t status,
             const zx_packet_signal_t* signal) { OnReady(status, signal); });

  return WaitOnReceive(ZX_OK);
}

void VirtioVsock::ChannelConnection::OnReady(zx_status_t status,
                                             const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on channel " << status;
    return;
  }

  // If the channel has been closed by the peer, move to sending a full
  // connection shutdown and wait on the Virtio receive queue.
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
    flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
    zx_signals_t signals = rx_wait_.trigger();
    rx_wait_.set_trigger(signals & ~ZX_CHANNEL_PEER_CLOSED);
    queue_callback_();
    return;
  }

  // If the channel is readable and our peer has buffer space, wait on the
  // Virtio receive queue.
  if (signal->observed & ZX_CHANNEL_READABLE && PeerFree() > 0) {
    queue_callback_();
  }
}

zx_status_t VirtioVsock::ChannelConnection::WriteCredit(
    virtio_vsock_hdr_t* header) {
  // TODO(PD-171): Once channel back-pressure has been implemented, we should
  // implement credit handling.
  constexpr size_t max = ZX_CHANNEL_MAX_MSG_BYTES;
  constexpr size_t used = 0;

  header->buf_alloc = max;
  header->fwd_cnt = rx_cnt_ - used;
  return ZX_OK;
}

zx_status_t VirtioVsock::ChannelConnection::Shutdown(uint32_t flags) {
  return ZX_OK;
}

zx_status_t VirtioVsock::ChannelConnection::Read(VirtioQueue* queue,
                                                 virtio_vsock_hdr_t* header,
                                                 virtio_desc_t* desc,
                                                 uint32_t* used) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    size_t len = std::min(desc->len, PeerFree());
    uint32_t actual;
    status = channel_.read(0, desc->addr, len, &actual, nullptr, 0, nullptr);
    if (status != ZX_OK) {
      // We are handling two different cases in this branch:
      // 1. If the channel is empty.
      // 2. If flow-control is suggesting the peer does not have enough space to
      //    receive this message, but the descriptor provided would have been
      //    been large enough for this message.
      //
      // In both cases, we should stop and return ZX_OK.
      //
      // TODO(PD-171): Figure out the best way to handle channel messages that
      // are larger than a single descriptor.
      if (status == ZX_ERR_SHOULD_WAIT ||
          (status == ZX_ERR_BUFFER_TOO_SMALL && desc->len > PeerFree())) {
        status = ZX_OK;
      } else {
        FXL_LOG(ERROR) << "Failed to read from channel " << status;
      }
      break;
    }

    *used += actual;
    tx_cnt_ += actual;
    if (PeerFree() == 0 || !desc->has_next) {
      break;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  header->len = *used;
  return status;
}

zx_status_t VirtioVsock::ChannelConnection::Write(VirtioQueue* queue,
                                                  virtio_vsock_hdr_t* header,
                                                  virtio_desc_t* desc) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    status = channel_.write(0, desc->addr, desc->len, nullptr, 0);
    rx_cnt_ += desc->len;
    header->len -= desc->len;
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write from channel " << status;
      UpdateOp(VIRTIO_VSOCK_OP_RST);
      return ZX_OK;
    }

    if (!desc->has_next || header->len == 0) {
      return ZX_OK;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  return status;
}

VirtioVsock::VirtioVsock(component::StartupContext* context,
                         const PhysMem& phys_mem,
                         async_dispatcher_t* dispatcher)
    : VirtioDeviceBase(phys_mem),
      dispatcher_(dispatcher),
      rx_stream_(dispatcher, rx_queue(), this),
      tx_stream_(dispatcher, tx_queue(), this) {
  config_.guest_cid = 0;
  if (context) {
    context->outgoing().AddPublicService(endpoint_bindings_.GetHandler(this));
  }
}

uint32_t VirtioVsock::guest_cid() const {
  fbl::AutoLock lock(&config_mutex_);
  return config_.guest_cid;
}

bool VirtioVsock::HasConnection(uint32_t src_cid, uint32_t src_port,
                                uint32_t dst_port) const {
  ConnectionKey key{src_cid, src_port, dst_port};
  fbl::AutoLock lock(&mutex_);
  return connections_.find(key) != connections_.end();
}

void VirtioVsock::SetContextId(
    uint32_t cid,
    fidl::InterfaceHandle<fuchsia::guest::HostVsockConnector> connector,
    fidl::InterfaceRequest<fuchsia::guest::GuestVsockAcceptor> acceptor) {
  {
    fbl::AutoLock lock(&config_mutex_);
    config_.guest_cid = cid;
  }
  acceptor_bindings_.AddBinding(this, std::move(acceptor));
  FXL_CHECK(connector_.Bind(std::move(connector)) == ZX_OK);
  tx_stream_.WaitOnQueue();
}

static fbl::unique_ptr<VirtioVsock::Connection> create_connection(
    zx::handle handle, async_dispatcher_t* dispatcher,
    fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
    fit::closure queue_callback) {
  zx_obj_type_t type = fsl::GetType(handle.get());
  switch (type) {
    case zx::socket::TYPE:
      return fbl::make_unique<VirtioVsock::SocketConnection>(
          std::move(handle), dispatcher, std::move(accept_callback),
          std::move(queue_callback));
    case zx::channel::TYPE:
      return fbl::make_unique<VirtioVsock::ChannelConnection>(
          std::move(handle), dispatcher, std::move(accept_callback),
          std::move(queue_callback));
    default:
      FXL_LOG(ERROR) << "Unexpected handle type " << type;
      return nullptr;
  }
}

void VirtioVsock::Accept(
    uint32_t src_cid, uint32_t src_port, uint32_t port, zx::handle handle,
    fuchsia::guest::GuestVsockAcceptor::AcceptCallback callback) {
  if (HasConnection(src_cid, src_port, port)) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  ConnectionKey key{src_cid, src_port, port};
  auto conn = create_connection(std::move(handle), dispatcher_,
                                std::move(callback), [this, key] {
                                  fbl::AutoLock lock(&mutex_);
                                  WaitOnQueueLocked(key);
                                });
  if (!conn) {
    callback(ZX_ERR_CONNECTION_REFUSED);
    return;
  }

  // From here on out the |conn| destructor will handle connection refusal upon
  // deletion.

  zx_status_t status = conn->Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to setup connection " << status;
    return;
  }

  fbl::AutoLock lock(&mutex_);
  AddConnectionLocked(key, std::move(conn));
}

void VirtioVsock::ConnectCallback(ConnectionKey key, zx_status_t status,
                                  zx::handle handle) {
  auto new_conn =
      create_connection(std::move(handle), dispatcher_, nullptr, [this, key] {
        fbl::AutoLock lock(&mutex_);
        WaitOnQueueLocked(key);
      });
  if (!new_conn) {
    new_conn = fbl::make_unique<NullConnection>();
  }
  Connection* conn = new_conn.get();

  {
    fbl::AutoLock lock(&mutex_);
    zx_status_t add_status = AddConnectionLocked(key, std::move(new_conn));
    if (add_status != ZX_OK) {
      return;
    }
    if (status != ZX_OK) {
      conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
      WaitOnQueueLocked(key);
      return;
    }
  }

  conn->UpdateOp(VIRTIO_VSOCK_OP_RESPONSE);
  status = conn->Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to setup connection " << status;
  }
}

zx_status_t VirtioVsock::AddConnectionLocked(ConnectionKey key,
                                             fbl::unique_ptr<Connection> conn) {
  bool inserted;
  std::tie(std::ignore, inserted) = connections_.emplace(key, std::move(conn));
  if (!inserted) {
    FXL_LOG(ERROR) << "Connection already exists";
    return ZX_ERR_ALREADY_EXISTS;
  }
  WaitOnQueueLocked(key);
  return ZX_OK;
}

VirtioVsock::Connection* VirtioVsock::GetConnectionLocked(ConnectionKey key) {
  auto it = connections_.find(key);
  return it == connections_.end() ? nullptr : it->second.get();
}

bool VirtioVsock::EraseOnErrorLocked(ConnectionKey key, zx_status_t status) {
  if (status != ZX_OK) {
    connections_.erase(key);
  }
  return status != ZX_OK;
}

void VirtioVsock::WaitOnQueueLocked(ConnectionKey key) {
  zx_status_t status = rx_stream_.WaitOnQueue();
  if (EraseOnErrorLocked(key, status)) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
    return;
  }
  readable_.insert(key);
}

static virtio_vsock_hdr_t* get_header(VirtioQueue* queue, uint16_t index,
                                      virtio_desc_t* desc, bool writable) {
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

static zx_status_t transmit(VirtioVsock::Connection* conn, VirtioQueue* queue,
                            virtio_vsock_hdr_t* header, virtio_desc_t* desc,
                            uint32_t* used) {
  switch (conn->op()) {
    case VIRTIO_VSOCK_OP_REQUEST:
      // We are sending a connection request, therefore we move to waiting
      // for response.
      conn->UpdateOp(VIRTIO_VSOCK_OP_RESPONSE);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RESPONSE:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      // We are sending a response or credit update, therefore we move to ready
      // to read/write.
      conn->UpdateOp(VIRTIO_VSOCK_OP_RW);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RW:
      // We are reading from the socket.
      return conn->Read(queue, header, desc, used);
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      header->flags = conn->flags();
      if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
        // We are sending a full connection shutdown, therefore we move to
        // waiting for a connection reset.
        conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
      } else {
        // One side of the connection is still active, therefore we move to
        // ready to read/write.
        conn->UpdateOp(VIRTIO_VSOCK_OP_RW);
      }
      return ZX_OK;
    default:
    case VIRTIO_VSOCK_OP_RST:
      // We are sending a connection reset, therefore remove the connection.
      header->op = VIRTIO_VSOCK_OP_RST;
      return ZX_ERR_STOP;
  }
}

void VirtioVsock::Mux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  bool index_valid = true;
  virtio_desc_t desc;
  fbl::AutoLock lock(&mutex_);
  for (auto i = readable_.begin(), end = readable_.end(); i != end;
       i = readable_.erase(i)) {
    Connection* conn = GetConnectionLocked(*i);
    if (conn == nullptr) {
      continue;
    }
    if (!index_valid) {
      status = rx_queue()->NextAvail(&index);
      if (status != ZX_OK) {
        return;
      }
    }
    virtio_vsock_hdr_t* header = get_header(rx_queue(), index, &desc, true);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from read queue";
      continue;
    }
    *header = {
        .src_cid = i->local_cid,
        .src_port = i->local_port,
        .dst_cid = guest_cid(),
        .dst_port = i->remote_port,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = conn->op(),
    };

    // If reading was shutdown, but we're still receiving a read request, send
    // a connection reset.
    if (conn->op() == VIRTIO_VSOCK_OP_RW &&
        conn->flags() & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
      conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
      FXL_LOG(ERROR) << "Receive was shutdown";
    }

    zx_status_t write_status = conn->WriteCredit(header);
    switch (write_status) {
      case ZX_OK:
        break;
      case ZX_ERR_UNAVAILABLE:
        status = conn->WaitOnTransmit(ZX_OK);
        if (EraseOnErrorLocked(*i, status)) {
          continue;
        }
        break;
      default:
        conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
        FXL_LOG(ERROR) << "Failed to write credit " << write_status;
        break;
    }

    uint32_t used = 0;
    status = transmit(conn, rx_queue(), header, &desc, &used);
    rx_queue()->Return(index, used + sizeof(*header));
    index_valid = false;
    status = conn->WaitOnReceive(status);
    EraseOnErrorLocked(*i, status);
  }

  // Release buffer if we did not have any readable connections to avoid a
  // descriptor leak.
  if (index_valid) {
    FXL_LOG(ERROR) << "Mux called with no readable connections. Descriptor "
                   << "will be returned with 0 length";
    rx_queue()->Return(index, 0);
  }
}

static void set_shutdown(virtio_vsock_hdr_t* header) {
  header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
  header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
}

static zx_status_t receive(VirtioVsock::Connection* conn, VirtioQueue* queue,
                           virtio_vsock_hdr_t* header, virtio_desc_t* desc) {
  switch (header->op) {
    case VIRTIO_VSOCK_OP_RESPONSE:
      return conn->Accept();
    case VIRTIO_VSOCK_OP_RW:
      // We are writing to the socket.
      return conn->Write(queue, header, desc);
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      // Credit update is handled outside of this function.
      return ZX_OK;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
      // We received a credit request, therefore we move to sending a credit
      // update.
      conn->UpdateOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RST:
      // We received a connection reset, therefore remove the connection.
      return ZX_ERR_STOP;
    default:
      header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
        // We received a full connection shutdown, therefore we move to sending
        // a connection reset.
        conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
        return ZX_OK;
      } else if (header->flags != 0) {
        return conn->Shutdown(header->flags);
      } else {
        FXL_LOG(ERROR) << "Connection shutdown with no shutdown flags set";
        return ZX_ERR_BAD_STATE;
      }
  }
}

void VirtioVsock::Demux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  virtio_desc_t desc;
  fbl::AutoLock lock(&mutex_);
  do {
    auto free_desc =
        fbl::MakeAutoCall([this, index]() { tx_queue()->Return(index, 0); });
    auto header = get_header(tx_queue(), index, &desc, false);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from write queue";
      return;
    } else if (header->type != VIRTIO_VSOCK_TYPE_STREAM) {
      set_shutdown(header);
      FXL_LOG(ERROR) << "Only stream sockets are supported";
    }
    ConnectionKey key{
        static_cast<uint32_t>(header->dst_cid),
        header->dst_port,
        header->src_port,
    };

    Connection* conn = GetConnectionLocked(key);
    if (header->op == VIRTIO_VSOCK_OP_REQUEST) {
      if (conn != nullptr) {
        // If a connection already exists, then the driver is in a bad state and
        // the connection should be shut down.
        set_shutdown(header);
        FXL_LOG(ERROR) << "Connection request for an existing connection";
      } else if (header->src_cid != guest_cid()) {
        // If the source CID does not match guest CID, then the driver is in a
        // bad state and the request should be ignored.
        FXL_LOG(ERROR) << "Source CID does not match guest CID";
        continue;
      } else if (connector_) {
        // We received a request for the guest to connect to an external CID.
        //
        // If we don't have a connector then implicitly just refuse any outbound
        // connections. Otherwise send out a request for a connection to the
        // remote CID.
        connector_->Connect(header->src_cid, header->src_port, header->dst_cid,
                            header->dst_port,
                            [this, key](zx_status_t status, zx::handle handle) {
                              ConnectCallback(key, status, std::move(handle));
                            });
        continue;
      }
    }

    if (conn == nullptr) {
      // Build a connection to send a connection reset.
      auto new_conn = fbl::make_unique<NullConnection>();
      conn = new_conn.get();
      status = AddConnectionLocked(key, std::move(new_conn));
      set_shutdown(header);
      FXL_LOG(ERROR) << "Connection does not exist";
    } else if (conn->op() == VIRTIO_VSOCK_OP_RW &&
               conn->flags() & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND) {
      // We are receiving a write, but send was shutdown.
      set_shutdown(header);
      FXL_LOG(ERROR) << "Send was shutdown";
    }

    conn->ReadCredit(header);
    status = receive(conn, tx_queue(), header, &desc);
    switch (conn->op()) {
      case VIRTIO_VSOCK_OP_RST:
      case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        WaitOnQueueLocked(key);
        break;
      default:
        status = conn->WaitOnTransmit(status);
        EraseOnErrorLocked(key, status);
        break;
    }
  } while (tx_queue()->NextAvail(&index) == ZX_OK);

  status = tx_stream_.WaitOnQueue();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
  }
}

}  // namespace machina
