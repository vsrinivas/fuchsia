// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/virtio_vsock.h"

#include <lib/async/cpp/task.h>
#include <lib/async/task.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/bit.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include "src/lib/fsl/handles/object_info.h"

namespace {

// Maximum number of unprocessed control packets the guest is allowed to cause
// us to generate before we stop emitting packets.
//
// In normal operation, this limit should never be reached: we only enqueue at
// most one outgoing packet per incoming packet, and the virtio protocol
// requires the guest to process received packets prior to sending any more.
constexpr size_t kMaxQueuedPackets = 10'000;

void SendResetPacket(VsockSendQueue& queue, const ConnectionKey& key) {
  queue.Write(virtio_vsock_hdr_t{
      .src_cid = key.local_cid,
      .dst_cid = key.remote_cid,
      .src_port = key.local_port,
      .dst_port = key.remote_port,
      .type = VIRTIO_VSOCK_TYPE_STREAM,
      .op = VIRTIO_VSOCK_OP_RST,
  });
}

}  // namespace

std::optional<VsockChain> VsockChain::FromQueue(VirtioQueue* queue, bool writable) {
  VirtioDescriptor desc;
  uint16_t index;

  // Read through descriptors on the queue until we find one that matches our
  // criteria, or run out.
  //
  // If the guest is functioning reasonably, we expect all incoming
  // descriptors to match our criteria.
  while (queue->NextAvail(&index) == ZX_OK) {
    zx_status_t status = queue->ReadDesc(index, &desc);
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to read descriptor from queue: " << status;
      queue->Return(index, /*len=*/0);
      continue;
    }

    // Ensure it has the correct read/write mode.
    if (desc.writable != writable) {
      FX_LOGS(ERROR) << "Descriptor is not " << (writable ? "writable" : "readable");
      queue->Return(index, /*len=*/0);
      continue;
    }

    // Ensure it is big enough.
    if (desc.len < sizeof(virtio_vsock_hdr_t)) {
      FX_LOGS(ERROR) << "Descriptor is too small";
      queue->Return(index, /*len=*/0);
      continue;
    }

    return VsockChain(queue, index, desc);
  }

  return std::nullopt;
}

void VsockChain::Release() {
  queue_ = nullptr;
  index_ = 0;
}

VsockChain::VsockChain(VsockChain&& other) noexcept { *this = std::move(other); }

VsockChain::~VsockChain() {
  FX_CHECK(queue_ == nullptr) << "VsockChain was destroyed without Return() being called.";
}

VsockChain& VsockChain::operator=(VsockChain&& other) noexcept {
  queue_ = other.queue_;
  desc_ = other.desc_;
  index_ = other.index_;
  other.Release();
  return *this;
}

virtio_vsock_hdr_t* VsockChain::header() const {
  FX_DCHECK(desc_.len >= sizeof(virtio_vsock_hdr_t));
  return static_cast<virtio_vsock_hdr_t*>(desc_.addr);
}

void VsockChain::Return(uint32_t used) {
  queue_->Return(index_, used);
  Release();
}

// Hash a ConnectionKey.
size_t ConnectionKey::Hash::operator()(const ConnectionKey& key) const {
  return ((static_cast<size_t>(key.local_cid) << 32) | key.local_port) ^
         (cpp20::rotl(static_cast<size_t>(key.remote_cid) << 32 | key.remote_port, 16));
}

VsockSendQueue::VsockSendQueue(VirtioQueue* queue) : queue_(queue) {}

std::optional<VsockChain> VsockSendQueue::StartWrite() {
  // Attempt to drain all queued packets.
  if (!Drain()) {
    return std::nullopt;
  }

  // Start a new transmit.
  return VsockChain::FromQueue(queue_, /*writable=*/true);
}

void VsockSendQueue::Write(const virtio_vsock_hdr_t& header) {
  // If we are able to drain all existing packets and another guest RX
  // descriptor is available, send the packet directly.
  if (Drain() && TryWritePacket(header)) {
    return;
  }

  // Otherwise, buffer the packet.
  send_buffer_.emplace_back(header);
}

bool VsockSendQueue::Drain() {
  while (!send_buffer_.empty()) {
    if (!TryWritePacket(send_buffer_.front())) {
      return false;
    }
    send_buffer_.pop_front();
  }
  return true;
}

bool VsockSendQueue::TryWritePacket(const virtio_vsock_hdr_t& packet) {
  std::optional<VsockChain> chain = VsockChain::FromQueue(queue_, /*writable=*/true);
  if (!chain.has_value()) {
    return false;
  }

  *chain->header() = packet;
  chain->Return(/*used=*/sizeof(virtio_vsock_hdr_t));
  return true;
}

// We take a |queue_callback| to decouple the connection from the device. This
// allows a connection to wait on a Virtio queue and update the device state,
// without having direct access to the device.
VirtioVsock::Connection::Connection(
    const ConnectionKey& key, zx::socket socket, async_dispatcher_t* dispatcher,
    fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
    fit::closure queue_callback)
    : dispatcher_(dispatcher),
      accept_callback_(std::move(accept_callback)),
      queue_callback_(std::move(queue_callback)),
      key_(key),
      socket_(std::move(socket)) {}

VirtioVsock::Connection::~Connection() {
  if (accept_callback_) {
    accept_callback_(ZX_ERR_CONNECTION_REFUSED);
  }

  // We must cancel the async wait before the socket is destroyed.
  rx_wait_.Cancel();
  tx_wait_.Cancel();
}

std::unique_ptr<VirtioVsock::Connection> VirtioVsock::Connection::Create(
    const ConnectionKey& key, zx::socket socket, async_dispatcher_t* dispatcher,
    fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
    fit::closure queue_callback) {
  // Using `new` to allow access to private constructor.
  return std::unique_ptr<VirtioVsock::Connection>(new VirtioVsock::Connection(
      key, std::move(socket), dispatcher, std::move(accept_callback), std::move(queue_callback)));
}

zx_status_t VirtioVsock::Connection::Init() {
  rx_wait_.set_object(socket_.get());
  rx_wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED |
                       ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);
  rx_wait_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                              const zx_packet_signal_t* signal) { OnReady(status, signal); });

  tx_wait_.set_object(socket_.get());
  tx_wait_.set_trigger(ZX_SOCKET_WRITABLE);
  tx_wait_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                              const zx_packet_signal_t* signal) { OnReady(status, signal); });

  return WaitOnReceive();
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
    return WaitOnReceive();
  }

  UpdateOp(VIRTIO_VSOCK_OP_RST);
  return ZX_OK;
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
  std::lock_guard<std::mutex> lock(op_update_mutex_);

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
      if (op_ == VIRTIO_VSOCK_OP_RESPONSE) {
        // NOTE: This is an invalid state.
        // We end up here when Mux and Demux race to update the state, and vsock
        // has essentially 'not yet completed connecting client' while trying to
        // 'report available credit'.
        // Do not update the op_ field here, as we risk that side handling
        // RESPONSE will never accept the client.
        FX_LOGS(INFO) << "Ignoring premature machine state change.";
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
  FX_LOGS(ERROR) << "Invalid state transition from " << op_ << " to " << new_op
                 << "; resetting connection";
  op_ = VIRTIO_VSOCK_OP_RST;
}

uint32_t VirtioVsock::Connection::PeerFree() const {
  // See 5.7.6.3 Buffer Space Management, from the Virtio Socket Device spec.
  return peer_buf_alloc_ - (tx_cnt_ - peer_fwd_cnt_);
}

void VirtioVsock::Connection::ReadCredit(virtio_vsock_hdr_t* header) {
  SetCredit(header->buf_alloc, header->fwd_cnt);
}

void VirtioVsock::Connection::SetCredit(uint32_t buf_alloc, uint32_t fwd_cnt) {
  peer_buf_alloc_ = buf_alloc;
  peer_fwd_cnt_ = fwd_cnt;
}

zx_status_t VirtioVsock::Connection::WaitOnTransmit() {
  if (tx_wait_.is_pending() || !tx_wait_.has_handler()) {
    return ZX_OK;
  }
  return tx_wait_.Begin(dispatcher_);
}

zx_status_t VirtioVsock::Connection::WaitOnReceive() {
  if (rx_wait_.is_pending() || !rx_wait_.has_handler()) {
    return ZX_OK;
  }
  return rx_wait_.Begin(dispatcher_);
}

void VirtioVsock::Connection::OnReady(zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed while waiting on socket " << status;
    return;
  }

  // If the socket is readable and our peer has buffer space, wait on the
  // Virtio receive queue. Do this before checking for peer closed so that
  // we first send any remaining data in the socket.
  if (signal->observed & ZX_SOCKET_READABLE && PeerFree() > 0) {
    queue_callback_();
    return;
  }

  // If the socket has been partially or fully closed, wait on the Virtio
  // receive queue.
  if (signal->observed &
      (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_WRITE_DISABLED)) {
    zx_signals_t signals = rx_wait_.trigger();
    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      // The peer closed the socket, therefore we move to sending a full
      // connection shutdown.
      UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
      flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      rx_wait_.set_trigger(signals & ~ZX_SOCKET_PEER_CLOSED);
    } else {
      if (signal->observed & ZX_SOCKET_PEER_WRITE_DISABLED &&
          !(flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV)) {
        // The peer disabled reading, therefore we move to sending a partial
        // connection shutdown.
        UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
        flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV;
        rx_wait_.set_trigger(signals & ~ZX_SOCKET_PEER_WRITE_DISABLED);
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

  // If the socket is writable and we last reported the buffer as full, send a
  // credit update message to the guest indicating buffer space is now
  // available.
  if (reported_buf_avail_ == 0 && signal->observed & ZX_SOCKET_WRITABLE) {
    UpdateOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
    queue_callback_();
  }
}

zx_status_t VirtioVsock::Connection::WriteCredit(virtio_vsock_hdr_t* header) {
  zx_info_socket_t info;
  zx_status_t status = socket_.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  header->buf_alloc = static_cast<uint32_t>(info.tx_buf_max);
  header->fwd_cnt = static_cast<uint32_t>(rx_cnt_ - info.tx_buf_size);
  reported_buf_avail_ = info.tx_buf_max - info.tx_buf_size;
  return reported_buf_avail_ != 0 ? ZX_OK : ZX_ERR_UNAVAILABLE;
}

zx_status_t VirtioVsock::Connection::Shutdown(uint32_t flags) {
  uint32_t disposition = 0;
  if (flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND) {
    disposition = ZX_SOCKET_DISPOSITION_WRITE_DISABLED;
  }
  uint32_t disposition_peer = 0;
  if (flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
    disposition_peer = ZX_SOCKET_DISPOSITION_WRITE_DISABLED;
  }
  return socket_.set_disposition(disposition, disposition_peer);
}

static zx_status_t setup_desc_chain(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                                    VirtioDescriptor* desc) {
  desc->addr = header + 1;
  desc->len -= sizeof(*header);
  // If the descriptor was only large enough for the header, read the next
  // descriptor, if there is one.
  if (desc->len == 0 && desc->has_next) {
    return queue->ReadDesc(desc->next, desc);
  }
  return ZX_OK;
}

zx_status_t VirtioVsock::Connection::Read(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                                          const VirtioDescriptor& desc, uint32_t* used) {
  VirtioDescriptor next = desc;
  zx_status_t status = setup_desc_chain(queue, header, &next);
  while (status == ZX_OK) {
    size_t len = std::min(next.len, PeerFree());
    size_t actual;
    status = socket_.read(0, next.addr, len, &actual);
    if (status != ZX_OK) {
      break;
    }

    *used += actual;
    tx_cnt_ += actual;
    if (PeerFree() == 0 || !next.has_next || actual < next.len) {
      break;
    }

    status = queue->ReadDesc(next.next, &next);
  }
  header->len = *used;
  return status;
}

zx_status_t VirtioVsock::Connection::Write(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                                           const VirtioDescriptor& desc) {
  VirtioDescriptor next = desc;
  zx_status_t status = setup_desc_chain(queue, header, &next);
  while (status == ZX_OK) {
    uint32_t len = std::min(next.len, header->len);
    size_t actual;
    status = socket_.write(0, next.addr, len, &actual);
    rx_cnt_ += actual;
    header->len -= actual;
    if (status != ZX_OK || actual < len) {
      // If we've failed to write just reset the connection. Note that it
      // should not be possible to receive a ZX_ERR_SHOULD_WAIT here if
      // the guest is honoring our credit messages that describe socket
      // buffer space.
      UpdateOp(VIRTIO_VSOCK_OP_RST);
      return ZX_OK;
    }

    reported_buf_avail_ -= actual;
    if (reported_buf_avail_ == 0 || !next.has_next || header->len == 0) {
      return ZX_OK;
    }

    status = queue->ReadDesc(next.next, &next);
  }
  return status;
}

VirtioVsock::VirtioVsock(sys::ComponentContext* context, const PhysMem& phys_mem,
                         async_dispatcher_t* dispatcher)
    : VirtioInprocessDevice("Virtio Vsock", phys_mem, 0 /* device_features */),
      dispatcher_(dispatcher),
      rx_queue_wait_(this, rx_queue()->event(), VirtioQueue::SIGNAL_QUEUE_AVAIL),
      tx_queue_wait_(this, tx_queue()->event(), VirtioQueue::SIGNAL_QUEUE_AVAIL),
      send_queue_(rx_queue()) {
  config_.guest_cid = 0;

  if (context) {
    context->outgoing()->AddPublicService(
        fidl::InterfaceRequestHandler<fuchsia::virtualization::GuestVsockEndpoint>(
            fit::bind_member(this, &VirtioVsock::Bind)));
  }
}

void VirtioVsock::Bind(
    fidl::InterfaceRequest<fuchsia::virtualization::GuestVsockEndpoint> request) {
  // Construct a request handler that posts a task to the VirtioVsock
  // dispatcher. VirtioVsock is not thread-safe and we must ensure that all
  // interactions with the endpoint binding set occur on the same thread.
  //
  // This handler will run on the initial thread, but other interactions run
  // on the "vsock-handler" thread. So we post a task to the dispatcher of the
  // async loop running on that thread.
  async::PostTask(dispatcher_, [this, request = std::move(request)]() mutable {
    endpoint_bindings_.AddBinding(this, std::move(request), dispatcher_);
  });
}

uint32_t VirtioVsock::guest_cid() const {
  std::lock_guard<std::mutex> lock(device_config_.mutex);
  return static_cast<uint32_t>(config_.guest_cid);
}

bool VirtioVsock::HasConnection(uint32_t src_cid, uint32_t src_port, uint32_t dst_port) const {
  ConnectionKey key{src_cid, src_port, guest_cid(), dst_port};
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.find(key) != connections_.end();
}

void VirtioVsock::SetContextId(
    uint32_t cid, fidl::InterfaceHandle<fuchsia::virtualization::HostVsockConnector> connector,
    fidl::InterfaceRequest<fuchsia::virtualization::GuestVsockAcceptor> acceptor) {
  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    config_.guest_cid = cid;
  }
  acceptor_bindings_.AddBinding(this, std::move(acceptor), dispatcher_);
  FX_CHECK(connector_.Bind(std::move(connector), dispatcher_) == ZX_OK);

  // Start waiting for incoming packets from the driver.
  zx_status_t status = tx_queue_wait_.Begin(dispatcher_);
  if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
    FX_LOGS(ERROR) << "Failed to wait on virtio TX queue: " << zx_status_get_string(status);
  }
}

void VirtioVsock::Accept(uint32_t src_cid, uint32_t src_port, uint32_t port, zx::handle handle,
                         fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback callback) {
  if (HasConnection(src_cid, src_port, port)) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }

  // Ensure the user gave us a socket handle.
  zx_obj_type_t type = fsl::GetType(handle.get());
  if (type != zx::socket::TYPE) {
    callback(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  ConnectionKey key{src_cid, src_port, guest_cid(), port};
  auto conn = Connection::Create(key, zx::socket(handle.release()), dispatcher_,
                                 std::move(callback), [this, key] {
                                   std::lock_guard<std::mutex> lock(mutex_);
                                   WaitOnQueueLocked(key);
                                 });
  if (!conn) {
    callback(ZX_ERR_CONNECTION_REFUSED);
    return;
  }

  // From here on out the |conn| destructor will handle connection refusal upon
  // deletion.

  std::lock_guard<std::mutex> lock(mutex_);
  AddConnectionLocked(key, std::move(conn));
}

void VirtioVsock::ConnectCallback(ConnectionKey key, zx_status_t status, zx::handle handle,
                                  uint32_t buf_alloc, uint32_t fwd_cnt) {
  // If the connection request resulted in an error, send a reset.
  if (status != ZX_OK) {
    std::lock_guard<std::mutex> lock(mutex_);
    SendResetPacket(send_queue_, key);
    return;
  }

  // If the host gave us an unsupported handle to communicate to them with, abort
  // the connection.
  zx_obj_type_t type = fsl::GetType(handle.get());
  if (type != zx::socket::TYPE) {
    std::lock_guard<std::mutex> lock(mutex_);
    SendResetPacket(send_queue_, key);
    return;
  }

  // Create a new connection object to track this virtio socket.
  std::unique_ptr<VirtioVsock::Connection> new_conn =
      Connection::Create(key, zx::socket(handle.release()), dispatcher_, nullptr, [this, key] {
        std::lock_guard<std::mutex> lock(mutex_);
        WaitOnQueueLocked(key);
      });
  if (!new_conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    SendResetPacket(send_queue_, key);
    return;
  }

  Connection* conn = new_conn.get();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    zx_status_t add_status = AddConnectionLocked(key, std::move(new_conn));
    if (add_status != ZX_OK) {
      return;
    }
  }

  conn->UpdateOp(VIRTIO_VSOCK_OP_RESPONSE);
  status = conn->Init();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to setup connection " << status;
  }
  conn->SetCredit(buf_alloc, fwd_cnt);
}

zx_status_t VirtioVsock::AddConnectionLocked(ConnectionKey key, std::unique_ptr<Connection> conn) {
  bool inserted;
  std::tie(std::ignore, inserted) = connections_.emplace(key, std::move(conn));
  if (!inserted) {
    FX_LOGS(ERROR) << "Connection already exists";
    return ZX_ERR_ALREADY_EXISTS;
  }
  WaitOnQueueLocked(key);
  return ZX_OK;
}

VirtioVsock::Connection* VirtioVsock::GetConnectionLocked(ConnectionKey key) {
  auto it = connections_.find(key);
  return it == connections_.end() ? nullptr : it->second.get();
}

void VirtioVsock::RemoveConnectionLocked(ConnectionKey key) {
  // Find the connection.
  auto it = connections_.find(key);
  FX_CHECK(it != connections_.end()) << "Attempted to erase unknown connection.";

  // Notify endpoints that it has been terminated.
  for (auto& binding : endpoint_bindings_.bindings()) {
    binding->events().OnShutdown(key.local_cid, key.local_port, guest_cid(), key.remote_port);
  }

  // Remove the connection.
  connections_.erase(it);
}

void VirtioVsock::WaitOnQueueLocked(ConnectionKey key) {
  zx_status_t status = rx_queue_wait_.Begin(dispatcher_);
  if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
    FX_LOGS(ERROR) << "Failed to wait on queue " << status;
    RemoveConnectionLocked(key);
    return;
  }
  readable_.insert(key);
}

zx_status_t VirtioVsock::Connection::Transmit(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                                              const VirtioDescriptor& desc, uint32_t* used) {
  // Write out the header.
  *header = {
      .src_cid = key_.local_cid,
      .dst_cid = key_.remote_cid,
      .src_port = key_.local_port,
      .dst_port = key_.remote_port,
      .type = VIRTIO_VSOCK_TYPE_STREAM,
      .op = op(),
  };

  // If reading was shutdown, but we're still receiving a read request, send
  // a connection reset.
  if (op() == VIRTIO_VSOCK_OP_RW && flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
    UpdateOp(VIRTIO_VSOCK_OP_RST);
    FX_LOGS(ERROR) << "Receive was shutdown";
  }

  zx_status_t write_status = WriteCredit(header);
  switch (write_status) {
    case ZX_OK:
      break;
    case ZX_ERR_UNAVAILABLE: {
      zx_status_t status = WaitOnTransmit();
      if (status != ZX_OK) {
        return ZX_ERR_STOP;
      }
      break;
    }
    default:
      UpdateOp(VIRTIO_VSOCK_OP_RST);
      FX_LOGS(ERROR) << "Failed to write credit " << write_status;
      break;
  }

  switch (op()) {
    case VIRTIO_VSOCK_OP_REQUEST:
      // We are sending a connection request, therefore we move to waiting
      // for response.
      UpdateOp(VIRTIO_VSOCK_OP_RESPONSE);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RESPONSE:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      // We are sending a response or credit update, therefore we move to ready
      // to read/write.
      UpdateOp(VIRTIO_VSOCK_OP_RW);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RW:
      // We are reading from the socket.
      return Read(queue, header, desc, used);
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      header->flags = flags_;
      if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
        // We are sending a full connection shutdown, therefore we move to
        // waiting for a connection reset.
        UpdateOp(VIRTIO_VSOCK_OP_RST);
      } else {
        // One side of the connection is still active, therefore we move to
        // ready to read/write.
        UpdateOp(VIRTIO_VSOCK_OP_RW);
      }
      return ZX_OK;
    default:
    case VIRTIO_VSOCK_OP_RST:
      // We are sending a connection reset, therefore remove the connection.
      header->op = VIRTIO_VSOCK_OP_RST;
      return ZX_ERR_STOP;
  }
}

bool VirtioVsock::ProcessReadyConnection(ConnectionKey key) {
  // Get the connection associated with the key.
  Connection* conn = GetConnectionLocked(key);
  if (conn == nullptr) {
    return true;
  }

  // Read an available chain.
  std::optional<VsockChain> chain = send_queue_.StartWrite();
  if (!chain.has_value()) {
    return false;
  }

  // Attempt to transmit data.
  uint32_t used = 0;
  zx_status_t transmit_status = conn->Transmit(rx_queue(), chain->header(), chain->desc(), &used);
  chain->Return(/*used=*/used + sizeof(virtio_vsock_hdr_t));

  // If the connection has been closed, remove it.
  if (transmit_status == ZX_ERR_STOP) {
    RemoveConnectionLocked(key);
    return true;
  }

  // Notify when the connection next has data pending.
  zx_status_t wait_status = conn->WaitOnReceive();
  if (wait_status != ZX_OK) {
    RemoveConnectionLocked(key);
  }

  return true;
}

void VirtioVsock::Mux(async_dispatcher_t*, async::WaitBase*, zx_status_t status,
                      const zx_packet_signal_t*) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error while waiting on virtio RX queue: " << zx_status_get_string(status);
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // Send any buffered control packets.
  send_queue_.Drain();

  // Process all connections that are ready to transmit, until we run out
  // of connections or descriptors in the guest's RX queue.
  for (auto i = readable_.begin(), end = readable_.end(); i != end; i = readable_.erase(i)) {
    bool continue_sending = ProcessReadyConnection(/*key=*/*i);
    if (!continue_sending || is_send_queue_full()) {
      break;
    }
  }

  // If we still have queued packets or connections waiting to send,
  // wait on more descriptors to arrive.
  if (!readable_.empty() || send_queue_.buffered_packets() > 0) {
    zx_status_t status = rx_queue_wait_.Begin(dispatcher_);
    if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
      FX_LOGS(ERROR) << "Failed to wait on RX queue: " << status;
    }
  }
}

static void set_shutdown(virtio_vsock_hdr_t* header) {
  header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
  header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
}

zx_status_t VirtioVsock::Connection::Receive(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                                             const VirtioDescriptor& desc) {
  // If we are getting a connection request for a connection that already
  // exists, then the driver is in a bad state and the connection should be
  // shut down.
  if (header->op == VIRTIO_VSOCK_OP_REQUEST) {
    set_shutdown(header);
    FX_LOGS(ERROR) << "Connection request for an existing connection";
  }

  // We are receiving a write, but send was shutdown.
  if (op() == VIRTIO_VSOCK_OP_RW && flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND) {
    set_shutdown(header);
    FX_LOGS(ERROR) << "Send was shutdown";
  }

  ReadCredit(header);

  switch (header->op) {
    case VIRTIO_VSOCK_OP_RESPONSE: {
      zx_status_t status = Init();
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to setup connection " << status;
        return status;
      }
      return Accept();
    }
    case VIRTIO_VSOCK_OP_RW:
      // We are writing to the socket.
      return Write(queue, header, desc);
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      // Credit update is handled outside of this function.
      return ZX_OK;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
      // We received a credit request, therefore we move to sending a credit
      // update.
      UpdateOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
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
        UpdateOp(VIRTIO_VSOCK_OP_RST);
        return ZX_OK;
      } else if (header->flags != 0) {
        return Shutdown(header->flags);
      } else {
        FX_LOGS(ERROR) << "Connection shutdown with no shutdown flags set";
        return ZX_ERR_BAD_STATE;
      }
  }
}

void VirtioVsock::ProcessIncomingPacket(const VsockChain& chain) {
  virtio_vsock_hdr_t* header = chain.header();
  ConnectionKey key{
      .local_cid = static_cast<uint32_t>(header->dst_cid),
      .local_port = header->dst_port,
      .remote_cid = guest_cid(),
      .remote_port = header->src_port,
  };

  // Reject packets with unknown socket types.
  if (header->type != VIRTIO_VSOCK_TYPE_STREAM) {
    FX_LOGS(ERROR) << "Guest sent socket packet with unknown type 0x" << std::hex << header->type;
    SendResetPacket(send_queue_, key);
    return;
  }

  // If the source CID does not match guest CID, then the driver is in a
  // bad state and the request should be ignored.
  if (header->src_cid != guest_cid()) {
    FX_LOGS(ERROR) << "Source CID does not match guest CID";
    return;
  }

  // Fetch the connection associated with this packet.
  Connection* conn = GetConnectionLocked(key);
  if (conn != nullptr) {
    // Process the packet.
    zx_status_t status = conn->Receive(tx_queue(), header, chain.desc());
    if (status != ZX_OK) {
      RemoveConnectionLocked(key);
      return;
    }

    // If the connection immediately needs to send an outgoing packet, add
    // the connection to the send queue.
    if (conn->op() == VIRTIO_VSOCK_OP_RST || conn->op() == VIRTIO_VSOCK_OP_CREDIT_UPDATE) {
      WaitOnQueueLocked(key);
      return;
    }

    // Wake up again when the connection next contains data.
    status = conn->WaitOnTransmit();
    if (status != ZX_OK) {
      RemoveConnectionLocked(key);
    }

    return;
  }

  // If we have a connector, handle new incoming connections.
  if (header->op == VIRTIO_VSOCK_OP_REQUEST && connector_) {
    connector_->Connect(static_cast<uint32_t>(header->src_cid), header->src_port,
                        static_cast<uint32_t>(header->dst_cid), header->dst_port,
                        [this, key, buf_alloc = header->buf_alloc, fwd_cnt = header->fwd_cnt](
                            zx_status_t status, zx::handle handle) {
                          ConnectCallback(key, status, std::move(handle), buf_alloc, fwd_cnt);
                        });
    return;
  }

  // Otherwise, reject the packet by sending a reset, unless the spurious
  // packet was a reset itself.
  FX_LOGS(WARNING) << "Received spurious packet from guest";
  if (header->op != VIRTIO_VSOCK_OP_RST) {
    SendResetPacket(send_queue_, key);
  }
}

void VirtioVsock::Demux(async_dispatcher_t*, async::WaitBase*, zx_status_t status,
                        const zx_packet_signal_t*) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error while waiting on virtio TX queue: " << zx_status_get_string(status);
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // If our outgoing queue is full, abort.
  //
  // Processing more incoming packets may cause even more outgoing packets to be
  // generated, and at this point the guest is unreasonably behind.
  if (is_send_queue_full()) {
    FX_LOGS(WARNING) << "Guest " << guest_cid()
                     << " not responding to sent vsock packets. Stopping receive.";
    return;
  }

  // Process all packets in the guest's TX queue.
  do {
    std::optional<VsockChain> chain = VsockChain::FromQueue(tx_queue(), /*writable=*/false);
    if (!chain.has_value()) {
      break;
    }

    ProcessIncomingPacket(*chain);

    chain->Return(/*used=*/0);
  } while (!is_send_queue_full());

  // Schedule this function to be called again next time the queue receives
  // a packet.
  status = tx_queue_wait_.Begin(dispatcher_);
  if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
    FX_LOGS(ERROR) << "Failed to wait on TX queue: " << status;
  }
}

bool VirtioVsock::is_send_queue_full() const {
  return send_queue_.buffered_packets() >= kMaxQueuedPackets;
}
