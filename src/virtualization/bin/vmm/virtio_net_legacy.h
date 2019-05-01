// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_NET_LEGACY_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_NET_LEGACY_H_

#include <fbl/unique_fd.h>
#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <trace-engine/types.h>
#include <virtio/net.h>
#include <virtio/virtio_ids.h>
#include <zircon/device/ethernet.h>

#include <atomic>
#include <vector>

#include "src/virtualization/bin/vmm/virtio_device.h"
#include "src/virtualization/bin/vmm/virtio_queue_waiter.h"

static constexpr uint16_t kVirtioNetLegacyNumQueues = 2;
static_assert(kVirtioNetLegacyNumQueues % 2 == 0,
              "There must be a queue for both RX and TX");

static constexpr uint16_t kVirtioNetLegacyRxQueueIndex = 0;
static constexpr uint16_t kVirtioNetLegacyTxQueueIndex = 1;
static_assert(kVirtioNetLegacyRxQueueIndex != kVirtioNetLegacyTxQueueIndex,
              "RX and TX queues must be distinct");

// Implements a Virtio Ethernet device.
class VirtioNetLegacy
    : public VirtioInprocessDevice<VIRTIO_ID_NET, kVirtioNetLegacyNumQueues,
                                   virtio_net_config_t> {
 public:
  VirtioNetLegacy(const PhysMem& phys_mem, async_dispatcher_t* dispatcher);
  ~VirtioNetLegacy() override;

  // Starts the Virtio Ethernet device based on the path provided.
  zx_status_t Start(const char* path);

  VirtioQueue* rx_queue() { return queue(kVirtioNetLegacyRxQueueIndex); }
  VirtioQueue* tx_queue() { return queue(kVirtioNetLegacyTxQueueIndex); }

 protected:
  // Helper function to initialize the IO bufs structure that gets shared with
  // the ethdriver. This is protected to allow for a mock VirtioNet to be easily
  // constructed for testing without needing a fully mocked ethernet driver.
  zx_status_t InitIoBuffer(size_t count, size_t elem_size);
  zx_status_t WaitOnFifos(const fuchsia_hardware_ethernet_Fifos& fifos);

 private:
  // Ethernet control plane.
  fuchsia_hardware_ethernet_Fifos fifos_ = {};
  // Connection to the Ethernet device.
  zx::channel net_svc_;

  std::atomic<trace_async_id_t>* rx_trace_flow_id() {
    return trace_flow_id(kVirtioNetLegacyRxQueueIndex);
  }
  std::atomic<trace_async_id_t>* tx_trace_flow_id() {
    return trace_flow_id(kVirtioNetLegacyTxQueueIndex);
  }

  class IoBuffer {
   public:
    IoBuffer() {}

    zx::vmo& vmo() { return vmo_; }

    zx_status_t Init(size_t count, size_t elem_size);
    zx_status_t Allocate(uintptr_t* offset);
    void Free(uintptr_t offset);

   private:
    std::vector<uint16_t> free_list_;
    size_t elem_size_;
    zx::vmo vmo_;
  };

  // A single data stream (either RX or TX).
  class Stream {
   public:
    Stream(const PhysMem& phys_mem, async_dispatcher_t* dispatcher,
           VirtioQueue* queue, std::atomic<trace_async_id_t>* trace_flow_id,
           IoBuffer* iobufs);
    zx_status_t Start(zx_handle_t fifo, size_t fifo_num_entries, bool rx);

   private:
    // Move buffers from VirtioQueue -> FIFO.
    zx_status_t WaitOnQueue();
    void OnQueueReady(zx_status_t status, uint16_t index);
    zx_status_t WaitOnFifoWritable();
    void OnFifoWritable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);

    // Return buffers from FIFO to VirtioQueue.
    zx_status_t WaitOnFifoReadable();
    void OnFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);

    virtio_net_hdr_t* ReadPacketInfo(uint16_t index, uintptr_t* offset,
                                     uintptr_t* length);

    const PhysMem& phys_mem_;
    async_dispatcher_t* dispatcher_;
    VirtioQueue* queue_;
    std::atomic<trace_async_id_t>* trace_flow_id_;
    zx_handle_t fifo_ = ZX_HANDLE_INVALID;
    bool rx_ = false;
    IoBuffer* io_buf_;

    std::vector<eth_fifo_entry_t> fifo_entries_;
    // Number of entries in |fifo_entries_| that have not yet been written
    // to the fifo.
    size_t fifo_num_entries_ = 0;
    // In the case of a short write to the fifo, we'll need to resume writing
    // from the middle of |fifo_entries_|. This is the index of the first item
    // to be written.
    size_t fifo_entries_write_index_ = 0;

    VirtioQueueWaiter queue_wait_;
    async::WaitMethod<Stream, &Stream::OnFifoWritable> fifo_writable_wait_{
        this};
    async::WaitMethod<Stream, &Stream::OnFifoReadable> fifo_readable_wait_{
        this};
  };

  Stream rx_stream_;
  Stream tx_stream_;

  IoBuffer io_buf_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_NET_LEGACY_H_
