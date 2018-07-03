// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_

#include <unordered_map>
#include <unordered_set>

#include <fuchsia/guest/cpp/fidl.h>
#include <virtio/virtio_ids.h>
#include <virtio/vsock.h>
#include <zx/socket.h>

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace machina {

static constexpr uint16_t kVirtioVsockNumQueues = 3;

class VirtioVsock
    : public VirtioDeviceBase<VIRTIO_ID_VSOCK, kVirtioVsockNumQueues,
                              virtio_vsock_config_t>,
      public fuchsia::guest::SocketEndpoint,
      public fuchsia::guest::SocketAcceptor {
 public:
  VirtioVsock(fuchsia::sys::StartupContext* context, const PhysMem&,
              async_t* async);

  uint32_t guest_cid() const;

  // Check whether a connection exists. The connection is identified by a local
  // tuple, local_cid/local_port, and a remote tuple, guest_cid/remote_port. The
  // local tuple identifies the host-side of the connection, and the remote
  // tuple identifies the guest-side of the connection.
  bool HasConnection(uint32_t local_cid, uint32_t local_port,
                     uint32_t remote_port) const;

  VirtioQueue* rx_queue() { return queue(0); }
  VirtioQueue* tx_queue() { return queue(1); }

  class Connection;

 private:
  // |fuchsia::guest::SocketEndpoint|
  void SetContextId(
      uint32_t cid,
      fidl::InterfaceHandle<fuchsia::guest::SocketConnector> connector,
      fidl::InterfaceRequest<fuchsia::guest::SocketAcceptor> acceptor) override;
  // |fuchsia::guest::SocketAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override;

  struct ConnectionKey {
    // The host-side of the connection is represented by local_cid and
    // local_port.
    uint32_t local_cid;
    uint32_t local_port;
    // The guest-side of the connection is represented by guest_cid and
    // remote_port.
    uint32_t remote_port;
    bool operator==(const ConnectionKey& key) const {
      return local_cid == key.local_cid && local_port == key.local_port &&
             remote_port == key.remote_port;
    }
  };
  struct ConnectionHash {
    size_t operator()(const ConnectionKey& key) const {
      return ((static_cast<size_t>(key.local_cid) << 32) | key.local_port) ^
             (key.remote_port << 16);
    }
  };
  using ConnectionMap =
      std::unordered_map<ConnectionKey, fbl::unique_ptr<Connection>,
                         ConnectionHash>;
  using ConnectionSet = std::unordered_set<ConnectionKey, ConnectionHash>;

  using StreamFunc = void (VirtioVsock::*)(zx_status_t, uint16_t);
  template <StreamFunc F>
  class Stream {
   public:
    Stream(async_t* async, VirtioQueue* queue, VirtioVsock* device);

    zx_status_t WaitOnQueue();

   private:
    VirtioQueueWaiter waiter_;
  };

  void ConnectCallback(ConnectionKey key, zx_status_t status,
                       zx::socket socket);
  zx_status_t SetupConnection(ConnectionKey key, Connection* conn);
  zx_status_t AddConnectionLocked(ConnectionKey key,
                                  fbl::unique_ptr<Connection> conn)
      __TA_REQUIRES(mutex_);
  Connection* GetConnectionLocked(ConnectionKey key) __TA_REQUIRES(mutex_);
  virtio_vsock_hdr_t* GetHeaderLocked(VirtioQueue* queue, uint16_t index,
                                      virtio_desc_t* desc, bool writable)
      __TA_REQUIRES(mutex_);

  template <StreamFunc F>
  zx_status_t WaitOnQueueLocked(ConnectionKey key, ConnectionSet* keys,
                                Stream<F>* stream) __TA_REQUIRES(mutex_);
  void WaitOnSocketLocked(zx_status_t status, ConnectionKey key,
                          async::Wait* wait) __TA_REQUIRES(mutex_);

  void OnSocketReady(async_t* async, async::Wait* wait, zx_status_t status,
                     const zx_packet_signal_t* signal, ConnectionKey key);

  zx_status_t Send(VirtioVsock::Connection* conn, virtio_vsock_hdr_t* header,
                   virtio_desc_t* desc, uint32_t* used);
  void Mux(zx_status_t status, uint16_t index);
  zx_status_t ReceiveLocked(ConnectionKey key, VirtioVsock::Connection* conn,
                            virtio_vsock_hdr_t* header, virtio_desc_t* desc)
      __TA_REQUIRES(mutex_);
  void Demux(zx_status_t status, uint16_t index);

  async_t* const async_;
  Stream<&VirtioVsock::Mux> rx_stream_;
  Stream<&VirtioVsock::Demux> tx_stream_;

  // TODO(PD-117): Evaluate granularity of locking.
  mutable fbl::Mutex mutex_;
  ConnectionMap connections_ __TA_GUARDED(mutex_);
  ConnectionSet readable_ __TA_GUARDED(mutex_);
  // NOTE(abdulla): We ignore the event queue, as we don't support VM migration.

  fidl::BindingSet<fuchsia::guest::SocketAcceptor> acceptor_bindings_;
  fidl::BindingSet<fuchsia::guest::SocketEndpoint> endpoint_bindings_;
  fuchsia::guest::SocketConnectorPtr connector_;
};

class VirtioVsock::Connection {
 public:
  ~Connection();

  // The number of bytes the guest expects us to have in our socket buffer.
  // This is the last credit_update sent minus any bytes we've received since
  // that update was sent.
  //
  // When this is 0 we'll need to send a CREDIT_UPDATE once buffer space has
  // been free'd so that the guest knows it can resume transmitting.
  size_t reported_buf_avail = 0;

  uint32_t flags = 0;
  uint32_t rx_cnt = 0;
  uint32_t tx_cnt = 0;
  uint32_t peer_buf_alloc = 0;
  uint32_t peer_fwd_cnt = 0;
  zx::socket socket;
  zx::socket remote_socket;
  async::Wait rx_wait;
  async::Wait tx_wait;
  fuchsia::guest::SocketAcceptor::AcceptCallback acceptor;

  uint16_t op() const { return op_; }
  void UpdateOp(uint16_t op);

  uint32_t peer_free() const {
    return peer_buf_alloc - (tx_cnt - peer_fwd_cnt);
  }

 private:
  uint16_t op_ = VIRTIO_VSOCK_OP_REQUEST;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
