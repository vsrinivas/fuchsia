// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_

#include <bitmap/rle-bitmap.h>
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

  bool HasConnection(uint32_t cid, uint32_t port) const;

  // Connect to a port on this device.
  //
  // @param src_cid Context ID of the source. This could be the context ID of
  //     the host, or the context ID of another guest.
  // @param src_port Port that we are connecting from outside the guest.
  // @param dst_port Port that we are connecting to within the guest. The
  //     context ID of the destination is based on the device configuration.
  zx_status_t Connect(uint32_t src_cid,
                      uint32_t src_port,
                      uint32_t dst_port,
                      zx::socket socket);

  // Listen on a port on this device. For each incoming connection from the
  // guest we invoke the connection acceptor which returns a port and socket for
  // the incoming connection.
  //
  // @param cid Context ID we are listening for. This could be the context ID of
  //     the host, or the context ID of another guest.
  // @param port Port that we are listening for.
  // @param acceptor Callback that is invoked each time a new connection is made
  //     in order to allocate a new port and socket for the connection.
  using ConnectionAcceptor = fbl::Function<std::pair<uint32_t, zx::socket>()>;
  zx_status_t Listen(uint32_t cid, uint32_t port, ConnectionAcceptor acceptor);

  VirtioQueue* rx_queue() { return queue(0); }
  VirtioQueue* tx_queue() { return queue(1); }

 private:
  struct Key {
    uint32_t cid;
    uint32_t port;
    bool operator==(const Key& key) const {
      return cid == key.cid && port == key.port;
    }
  };
  struct Hash {
    size_t operator()(const Key& key) const {
      return (static_cast<size_t>(key.cid) << 32) | key.port;
    }
  };
  struct Connection {
    uint32_t dst_cid;
    uint32_t dst_port;
    ConnectionAcceptor acceptor;
    zx::socket socket;
    async::Wait rx_wait;
    async::Wait tx_wait;
    uint16_t op;
    uint32_t flags;
  };
  using ConnectionMap =
      std::unordered_map<Key, fbl::unique_ptr<Connection>, Hash>;
  using KeySet = std::unordered_set<Key, Hash>;

  using StreamFunc = void (VirtioVsock::*)(zx_status_t, uint16_t);
  template <StreamFunc F>
  class Stream {
   public:
    Stream(async_t* async, VirtioQueue* queue) : queue_wait_(async, queue) {}
    ~Stream() { queue_wait_.Cancel(); }

    zx_status_t WaitOnQueue(VirtioVsock* vsock);

   private:
    VirtioQueueWaiter queue_wait_;
  };

  zx_status_t AddConnectionLocked(Key key, fbl::unique_ptr<Connection> conn)
      __TA_REQUIRES(mutex_);
  Connection* GetConnectionLocked(Key key) __TA_REQUIRES(mutex_);
  virtio_vsock_hdr_t* GetHeaderLocked(VirtioQueue* queue,
                                      uint16_t index,
                                      virtio_desc_t* desc,
                                      bool writable) __TA_REQUIRES(mutex_);

  template <StreamFunc F>
  zx_status_t WaitOnQueueLocked(Key key, KeySet* keys, Stream<F>* stream)
      __TA_REQUIRES(mutex_);
  void WaitOnSocketLocked(zx_status_t status, Key key, async::Wait* wait)
      __TA_REQUIRES(mutex_);

  async_wait_result_t OnSocketReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal_t* signal,
                                    Key key);

  void Mux(zx_status_t status, uint16_t index);
  void Demux(zx_status_t status, uint16_t index);

  async_t* const async_;
  // TODO(PD-117): Evaluate granularity of locking.
  mutable fbl::Mutex mutex_;
  ConnectionMap connections_ __TA_GUARDED(mutex_);
  KeySet readable_ __TA_GUARDED(mutex_);
  KeySet writable_ __TA_GUARDED(mutex_);
  Stream<&VirtioVsock::Mux> rx_stream_;
  Stream<&VirtioVsock::Demux> tx_stream_;
  // NOTE(abdulla): We ignore the event queue, as we don't support VM migration.
};

// A helper to allocate ephemeral ports for connections.
class EphemeralPortAllocator {
 public:
  zx_status_t Alloc(uint32_t* port);
  zx_status_t Free(uint32_t port);

 private:
  fbl::Mutex mutex_;
  bitmap::RleBitmap bitmap_ __TA_GUARDED(mutex_);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
