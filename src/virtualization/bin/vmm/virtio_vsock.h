// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_VSOCK_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_VSOCK_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/socket.h>

#include <deque>
#include <unordered_map>
#include <unordered_set>

#include <virtio/virtio_ids.h>
#include <virtio/vsock.h>

#include "src/virtualization/bin/vmm/virtio_device.h"
#include "src/virtualization/bin/vmm/virtio_queue_waiter.h"

static constexpr uint16_t kVirtioVsockNumQueues = 3;

// VsockChain is a thin wrapper around a VirtioDescriptor and associated index.
//
// TODO(fxbug.dev/85702): Replace with VirtioChain when possible.
class VsockChain {
 public:
  ~VsockChain();

  // Read a VsockChain from the given queue.
  //
  // The function discards any invalid descriptors, returning either a
  // descriptor or std::nullopt if the no descriptors are available on the queue.
  //
  // Return std::nullopt if no descriptors were available on the queue.
  static std::optional<VsockChain> FromQueue(VirtioQueue* queue, bool writable);

  // Get the head descriptor of this chain.
  const VirtioDescriptor& desc() const { return desc_; }

  // Get the parent queue of this chain.
  VirtioQueue* queue() const { return queue_; }

  // Return a pointer to the virtio_vsock_hdr_t header associated with this chain.
  //
  // This points to the first sizeof(virtio_vsock_hdr_t) bytes of the chain's payload.
  virtio_vsock_hdr_t* header() const;

  // Return this chain back to the origin queue.
  //
  // Must be called prior to destruction.
  void Return(uint32_t used);

  // Move only.
  VsockChain(VsockChain&&) noexcept;
  VsockChain& operator=(VsockChain&&) noexcept;

 private:
  VsockChain(VirtioQueue* queue, uint16_t index, const VirtioDescriptor& desc)
      : queue_(queue), index_(index), desc_(desc) {}

  // Release ownership of the chain.
  void Release();

  VirtioQueue* queue_ = nullptr;  // Source VirtioQueue.
  uint16_t index_ = 0;            // Index of the head descriptor.
  VirtioDescriptor desc_{};       // Head descriptor.
};

// ConnectionKey stores the source/destination cid/ports of a connection.
struct ConnectionKey {
  // The host-side of the connection is represented by local_cid and
  // local_port.
  uint32_t local_cid;
  uint32_t local_port;

  // The guest-side of the connection is represented by remote_cid and
  // remote_port.
  uint32_t remote_cid;
  uint32_t remote_port;

  bool operator==(const ConnectionKey& key) const {
    return local_cid == key.local_cid && local_port == key.local_port &&
           remote_cid == key.remote_cid && remote_port == key.remote_port;
  }

  struct Hash {
    size_t operator()(const ConnectionKey& key) const;
  };
};

// Allows direct sends to a VirtIO queue, buffering if required.
//
// Buffered packets will not automatically be sent, but will be retried
// next time StartWrite(), Write() or Drain() are called.
class VsockSendQueue {
 public:
  explicit VsockSendQueue(VirtioQueue* queue);

  // Return a VsockChain to the virtio queue if available.
  //
  // Drains buffered packets first to ensure FIFO ordering is maintained.
  std::optional<VsockChain> StartWrite();

  // Write a header-only packet to the queue, buffering it if no descriptors
  // are available.
  void Write(const virtio_vsock_hdr_t& header);

  // Write out buffered packets.
  //
  // Return true if all buffered packets packets have been successfully
  // sent.
  bool Drain();

  // Get the number of buffered packets waiting to be sent.
  size_t buffered_packets() const { return send_buffer_.size(); }

 private:
  // Attempt to write the header-only packet to the queue.
  //
  // Returns true on success.
  bool TryWritePacket(const virtio_vsock_hdr_t& packet);

  VirtioQueue* queue_;
  std::deque<virtio_vsock_hdr_t> send_buffer_;  // Buffered metadata packets for sending.
};

class VirtioVsock
    : public VirtioInprocessDevice<VIRTIO_ID_VSOCK, kVirtioVsockNumQueues, virtio_vsock_config_t>,
      public fuchsia::virtualization::GuestVsockEndpoint,
      public fuchsia::virtualization::GuestVsockAcceptor {
 public:
  VirtioVsock(sys::ComponentContext* context, const PhysMem&, async_dispatcher_t* dispatcher);

  // Bind to the given GuestVsockEndpoint interface request.
  void Bind(fidl::InterfaceRequest<fuchsia::virtualization::GuestVsockEndpoint> request);

  uint32_t guest_cid() const;

  // Check whether a connection exists. The connection is identified by a local
  // tuple, local_cid/local_port, and a remote tuple, guest_cid/remote_port. The
  // local tuple identifies the host-side of the connection, and the remote
  // tuple identifies the guest-side of the connection.
  bool HasConnection(uint32_t local_cid, uint32_t local_port, uint32_t remote_port) const;

  VirtioQueue* rx_queue() { return queue(0); }
  VirtioQueue* tx_queue() { return queue(1); }

  class Connection;

 private:
  using ConnectionMap =
      std::unordered_map<ConnectionKey, std::unique_ptr<Connection>, ConnectionKey::Hash>;
  using ConnectionSet = std::unordered_set<ConnectionKey, ConnectionKey::Hash>;

  // |fuchsia::virtualization::GuestVsockEndpoint|
  void SetContextId(
      uint32_t cid, fidl::InterfaceHandle<fuchsia::virtualization::HostVsockConnector> connector,
      fidl::InterfaceRequest<fuchsia::virtualization::GuestVsockAcceptor> acceptor) override;
  // |fuchsia::virtualization::GuestVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port, zx::handle handle,
              fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback callback) override;
  void ConnectCallback(ConnectionKey key, zx_status_t status, zx::handle handle, uint32_t buf_alloc,
                       uint32_t fwd_cnt);

  zx_status_t AddConnectionLocked(ConnectionKey key, std::unique_ptr<Connection> conn)
      __TA_REQUIRES(mutex_);
  Connection* GetConnectionLocked(ConnectionKey key) __TA_REQUIRES(mutex_);

  // Release resources associated with the given connection, and notify all bound
  // GuestVsockEndpoint's of the termination.
  void RemoveConnectionLocked(ConnectionKey key) __TA_REQUIRES(mutex_);

  void WaitOnQueueLocked(ConnectionKey key) __TA_REQUIRES(mutex_);

  // Process an incoming packet from the guest.
  void ProcessIncomingPacket(const VsockChain& chain) __TA_REQUIRES(mutex_);

  // Process a ready-to-send connection, writing any pending data to the
  // guest's RX queue.
  //
  // Returns true if the connection was processed (and more connections can be
  // processed), or false if now descriptors were available in the guest's RX
  // queue.
  bool ProcessReadyConnection(ConnectionKey key) __TA_REQUIRES(mutex_);

  void Mux(async_dispatcher_t*, async::WaitBase*, zx_status_t, const zx_packet_signal_t*);
  void Demux(async_dispatcher_t*, async::WaitBase*, zx_status_t, const zx_packet_signal_t*);

  // Return true if the number of buffered messages exceeds a maximum threshold.
  bool is_send_queue_full() const;

  async_dispatcher_t* const dispatcher_;

  // Waiter objects notifying us when the TX/RX virtio queues are ready.
  async::WaitMethod<VirtioVsock, &VirtioVsock::Mux> rx_queue_wait_;
  async::WaitMethod<VirtioVsock, &VirtioVsock::Demux> tx_queue_wait_;

  // TODO(fxbug.dev/12407): Evaluate granularity of locking.
  mutable std::mutex mutex_;
  ConnectionMap connections_ __TA_GUARDED(mutex_);
  ConnectionSet readable_ __TA_GUARDED(mutex_);
  // NOTE(abdulla): We ignore the event queue, as we don't support VM migration.

  fidl::BindingSet<fuchsia::virtualization::GuestVsockEndpoint> endpoint_bindings_;
  fidl::BindingSet<fuchsia::virtualization::GuestVsockAcceptor> acceptor_bindings_;
  fuchsia::virtualization::HostVsockConnectorPtr connector_;

  VsockSendQueue send_queue_;
};

class VirtioVsock::Connection {
 public:
  ~Connection();

  // Create a new Connection object.
  static std::unique_ptr<VirtioVsock::Connection> Create(
      const ConnectionKey& key, zx::socket socket, async_dispatcher_t* dispatcher,
      fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
      fit::closure queue_callback);

  zx_status_t Init();

  uint16_t op() const {
    std::lock_guard<std::mutex> lock(op_update_mutex_);
    return op_;
  }

  // Process an incoming packet, updating internal state as required.
  //
  // Returns ZX_OK on success. On error, the connection be terminated.
  zx_status_t Receive(VirtioQueue* queue, virtio_vsock_hdr_t* header, const VirtioDescriptor& desc);

  // Send an outgoing packet to the given descriptor.
  //
  // Returns ZX_OK on success. On error, the connection should be shut down.
  zx_status_t Transmit(VirtioQueue* queue, virtio_vsock_hdr_t* header, const VirtioDescriptor& desc,
                       uint32_t* used);

  void UpdateOp(uint16_t op);

  void SetCredit(uint32_t buf_alloc, uint32_t fwd_cnt);

  zx_status_t WaitOnTransmit();
  zx_status_t WaitOnReceive();

 private:
  Connection(const ConnectionKey& key, zx::socket socket, async_dispatcher_t* dispatcher,
             fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
             fit::closure queue_callback);

  zx_status_t Accept();

  uint32_t PeerFree() const;

  // Read credit from the header.
  void ReadCredit(virtio_vsock_hdr_t* header);

  // Write credit to the header. If this function returns:
  // - ZX_OK, it indicates to the device that it was successful.
  // - ZX_ERR_UNAVAILABLE, it indicates to the device that there is no buffer
  //   available, and should wait for the connection to transmit data.
  // - Anything else, it indicates to the device the connection should be reset.
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header);

  zx_status_t Shutdown(uint32_t flags);
  zx_status_t Read(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                   const VirtioDescriptor& desc, uint32_t* used);
  zx_status_t Write(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                            const VirtioDescriptor& desc);

  void OnReady(zx_status_t status, const zx_packet_signal_t* signal);

  mutable std::mutex op_update_mutex_;

  async_dispatcher_t* dispatcher_;

  uint32_t flags_ = 0;
  uint32_t rx_cnt_ = 0;
  uint32_t tx_cnt_ = 0;
  uint32_t peer_buf_alloc_ = 0;
  uint32_t peer_fwd_cnt_ = 0;
  uint16_t op_ __TA_GUARDED(op_update_mutex_) = VIRTIO_VSOCK_OP_REQUEST;

  // The number of bytes the guest expects us to have in our socket buffer.
  // This is the last credit_update sent minus any bytes we've received since
  // that update was sent.
  //
  // When this is 0 we'll need to send a CREDIT_UPDATE once buffer space has
  // been free'd so that the guest knows it can resume transmitting.
  size_t reported_buf_avail_ = 0;

  fuchsia::virtualization::GuestVsockAcceptor::AcceptCallback accept_callback_;

  // Callback triggered when data is available on the socket.
  fit::closure queue_callback_;

  // Source/dest port/cids associated with this connection.
  const ConnectionKey key_;

  // Notification objects for when the Zircon socket has data ready on it
  // and when it has space available for writing to.
  async::Wait rx_wait_;
  async::Wait tx_wait_;

  // The Zircon socket we are marshalling data to/from.
  zx::socket socket_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_VSOCK_H_
