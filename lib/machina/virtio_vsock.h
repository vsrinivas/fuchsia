// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_

#include <virtio/virtio_ids.h>
#include <virtio/vsock.h>
#include <zx/socket.h>
#include <unordered_map>
#include <unordered_set>

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"

namespace machina {

static constexpr uint16_t kVirtioVsockNumQueues = 3;
static constexpr uint32_t kVirtioVsockHostCid = 2;

class VirtioVsock : public VirtioDeviceBase<VIRTIO_ID_VSOCK,
                                            kVirtioVsockNumQueues,
                                            virtio_vsock_config_t> {
 public:
  VirtioVsock(const PhysMem&, async_t* async, uint32_t guest_cid);

  uint32_t guest_cid() const;

  // Check whether a connection exists. The connection is identified by a local
  // tuple, local_cid/local_port, and a remote tuple, guest_cid/remote_port. The
  // local tuple identifies the host-side of the connection, and the remote
  // tuple identifies the guest-side of the connection.
  bool HasConnection(uint32_t local_cid,
                     uint32_t local_port,
                     uint32_t remote_port) const;

  // Connect to a port on this device. This implies that the guest is listening
  // on guest_cid with remote_port.
  //
  // @param local_cid Context ID of the host-side. This could be the context ID
  //     of the host, or the context ID of another guest.
  // @param local_port Port that we are connecting from outside the guest.
  // @param remote_port Port that we are connecting to within the guest. The
  //     context ID of the guest-side is based on the device configuration.
  // @param socket Socket to use for this connection.
  zx_status_t Connect(uint32_t local_cid,
                      uint32_t local_port,
                      uint32_t remote_port,
                      zx::socket socket);

  // Listen on a port on this device. For each incoming connection from the
  // guest we invoke the connection acceptor, which returns a socket for the
  // incoming connection.
  //
  // @param cid Context ID we are listening for. This could be the context ID of
  //     the host, or the context ID of another guest.
  // @param port Port that we are listening for.
  // @param socket Socket to use for this connection.
  // @param acceptor Callback that is invoked for each new connection.
  using ConnectionAcceptor = fbl::Function<zx_status_t(zx::socket*)>;
  zx_status_t Listen(uint32_t cid, uint32_t port, ConnectionAcceptor acceptor);

  VirtioQueue* rx_queue() { return queue(0); }
  VirtioQueue* tx_queue() { return queue(1); }

 private:
  struct ListenerKey {
    uint32_t cid;
    uint32_t port;
    bool operator==(const ListenerKey& key) const {
      return cid == key.cid && port == key.port;
    }
  };
  struct ListenerHash {
    size_t operator()(const ListenerKey& key) const {
      return (static_cast<size_t>(key.cid) << 32) | key.port;
    }
  };
  using ListenerMap =
      std::unordered_map<ListenerKey, ConnectionAcceptor, ListenerHash>;

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
    ListenerKey listener_key() const { return {local_cid, local_port}; }
  };
  struct ConnectionHash {
    size_t operator()(const ConnectionKey& key) const {
      return ((static_cast<size_t>(key.local_cid) << 32) | key.local_port) ^
             (key.remote_port << 16);
    }
  };
  struct Connection {
    uint16_t op = 0;
    uint32_t flags = 0;
    zx::socket socket;
    async::Wait rx_wait;
    async::Wait tx_wait;
  };
  using ConnectionMap = std::
      unordered_map<ConnectionKey, fbl::unique_ptr<Connection>, ConnectionHash>;
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

  zx_status_t SetupConnection(ConnectionKey key, Connection* conn);
  zx_status_t AddConnectionLocked(ConnectionKey key,
                                  fbl::unique_ptr<Connection> conn)
      __TA_REQUIRES(mutex_);
  Connection* GetConnectionLocked(ConnectionKey key) __TA_REQUIRES(mutex_);
  ConnectionAcceptor* GetAcceptorLocked(ListenerKey key) __TA_REQUIRES(mutex_);
  virtio_vsock_hdr_t* GetHeaderLocked(VirtioQueue* queue,
                                      uint16_t index,
                                      virtio_desc_t* desc,
                                      bool writable) __TA_REQUIRES(mutex_);

  template <StreamFunc F>
  zx_status_t WaitOnQueueLocked(ConnectionKey key,
                                ConnectionSet* keys,
                                Stream<F>* stream) __TA_REQUIRES(mutex_);
  void WaitOnSocketLocked(zx_status_t status,
                          ConnectionKey key,
                          async::Wait* wait) __TA_REQUIRES(mutex_);

  void OnSocketReady(async_t* async,
                     async::Wait* wait,
                     zx_status_t status,
                     const zx_packet_signal_t* signal,
                     ConnectionKey key);

  void Mux(zx_status_t status, uint16_t index);
  void Demux(zx_status_t status, uint16_t index);

  async_t* const async_;
  Stream<&VirtioVsock::Mux> rx_stream_;
  Stream<&VirtioVsock::Demux> tx_stream_;

  // TODO(PD-117): Evaluate granularity of locking.
  mutable fbl::Mutex mutex_;
  ConnectionMap connections_ __TA_GUARDED(mutex_);
  ConnectionSet readable_ __TA_GUARDED(mutex_);
  ConnectionSet writable_ __TA_GUARDED(mutex_);
  ListenerMap listeners_ __TA_GUARDED(mutex_);
  // NOTE(abdulla): We ignore the event queue, as we don't support VM migration.
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
