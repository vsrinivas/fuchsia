// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_net.h"

#include <virtio/virtio_ids.h>
#include <zircon/device/ethernet.h>

#include <fcntl.h>
#include <string.h>

#include "lib/fxl/logging.h"

namespace machina {

VirtioNet::VirtioNet(const PhysMem& phys_mem, async_t* async)
    : VirtioDevice(VIRTIO_ID_NET,
                   &config_,
                   sizeof(config_),
                   queues_,
                   kNumQueues,
                   phys_mem),
      async_(async) {
  config_.status = VIRTIO_NET_S_LINK_UP;
  config_.max_virtqueue_pairs = 1;
  // TODO(abdulla): Support VIRTIO_NET_F_STATUS via IOCTL_ETHERNET_GET_STATUS.
  add_device_features(VIRTIO_NET_F_MAC);
}

VirtioNet::~VirtioNet() {
  tx_wait_.Cancel(async_);
  rx_wait_.Cancel(async_);
  zx_handle_close(fifos_.tx_fifo);
  zx_handle_close(fifos_.rx_fifo);
}

zx_status_t VirtioNet::Start(const char* path) {
  net_fd_.reset(open(path, O_RDONLY));
  if (!net_fd_) {
    return ZX_ERR_IO;
  }

  eth_info_t info;
  ssize_t ret = ioctl_ethernet_get_info(net_fd_.get(), &info);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to get Ethernet device info";
    return ret;
  }
  // TODO(abdulla): Use a different MAC address from the host.
  memcpy(config_.mac, info.mac, sizeof(config_.mac));

  ret = ioctl_ethernet_get_fifos(net_fd_.get(), &fifos_);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to get FIFOs from Ethernet device";
    return ret;
  }

  // TODO(ZX-1333): Limit how much of they guest physical address space
  // is exposed to the Ethernet server.
  zx_handle_t vmo;
  zx_status_t status =
      zx_handle_duplicate(phys_mem().vmo().get(), ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate guest physical memory";
    return status;
  }
  ret = ioctl_ethernet_set_iobuf(net_fd_.get(), &vmo);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set VMO for Ethernet device";
    zx_handle_close(vmo);
    return ret;
  }
  ret = ioctl_ethernet_set_client_name(net_fd_.get(), "machina", 7);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set client name for Ethernet device";
    return ret;
  }
  ret = ioctl_ethernet_start(net_fd_.get());
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to start communication with Ethernet device";
    return ret;
  }

  thrd_t tx_thread;
  ret = thrd_create_with_name(
      &tx_thread,
      [](void* ctx) -> int {
        return static_cast<VirtioNet*>(ctx)->TransmitLoop();
      },
      this, "virtio-net-transmit");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(tx_thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  thrd_t rx_thread;
  ret = thrd_create_with_name(
      &rx_thread,
      [](void* ctx) -> int {
        return static_cast<VirtioNet*>(ctx)->ReceiveLoop();
      },
      this, "virtio-net-receive");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(rx_thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  // Setup async tasks to return descriptors to the queue.
  status = WaitOnFifos(fifos_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on ethernet fifos";
    return status;
  }

  FXL_LOG(INFO) << "Polling device " << path << " for Ethernet frames";
  return ZX_OK;
}

zx_status_t VirtioNet::WaitOnFifos(const eth_fifos_t& fifos) {
  // Setup async tasks to return descriptors to the queue.
  zx_status_t status =
      FifoWait(&tx_wait_, fifos.tx_fifo, fifos.tx_depth, tx_queue());
  if (status == ZX_OK) {
    status = FifoWait(&rx_wait_, fifos.rx_fifo, fifos.rx_depth, rx_queue());
  }
  return status;
}

zx_status_t VirtioNet::FifoWait(async::Wait* wait,
                                zx_handle_t fifo,
                                size_t fifo_depth,
                                VirtioQueue* queue) {
  wait->set_object(fifo);
  wait->set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
  wait->set_handler(
      [this, fifo, fifo_depth, queue](async_t* async, zx_status_t status,
                                      const zx_packet_signal_t* signal) {
        if (status == ZX_OK) {
          status = DrainFifo(fifo, fifo_depth, queue);
        }
        return status == ZX_OK ? ASYNC_WAIT_AGAIN : ASYNC_WAIT_FINISHED;
      });
  return wait->Begin(async_);
}

zx_status_t VirtioNet::DrainFifo(zx_handle_t fifo,
                                 size_t fifo_depth,
                                 VirtioQueue* queue) {
  eth_fifo_entry_t entries[fifo_depth];

  // Dequeue entries for the Ethernet device.
  uint32_t count;
  zx_status_t status = zx_fifo_read(fifo, entries, sizeof(entries), &count);
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ZX_OK;
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read from fifo";
    return status;
  }
  for (uint32_t i = 0; i < count; i++) {
    auto head = reinterpret_cast<uintptr_t>(entries[i].cookie);
    auto length = entries[i].length + sizeof(virtio_net_hdr_t);
    queue->Return(head, length);
  }

  // Notify guest of updates to the queue.
  return NotifyGuest();
}

zx_status_t VirtioNet::DrainQueue(VirtioQueue* queue,
                                  uint32_t max_entries,
                                  zx_handle_t fifo,
                                  bool rx) {
  eth_fifo_entry_t entries[max_entries];

  // Wait on first descriptor chain to become available.
  uint16_t head;
  queue->Wait(&head);

  // Read all available descriptor chains from the queue.
  zx_status_t status = ZX_OK;
  uint32_t num_entries = 0;
  for (; num_entries < max_entries && status == ZX_OK; num_entries++) {
    virtio_desc_t desc;
    status = queue->ReadDesc(head, &desc);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to read descriptor from queue";
      return status;
    }

    uintptr_t packet_offset;
    uintptr_t packet_length;
    auto header = reinterpret_cast<virtio_net_hdr_t*>(desc.addr);
    if (!desc.has_next) {
      packet_offset = static_cast<uint32_t>(
          reinterpret_cast<uintptr_t>(header + 1) - phys_mem().addr());
      packet_length = static_cast<uint16_t>(desc.len - sizeof(*header));
    } else if (desc.len == sizeof(virtio_net_hdr_t)) {
      status = queue->ReadDesc(desc.next, &desc);
      packet_offset = static_cast<uint32_t>(
          reinterpret_cast<uintptr_t>(desc.addr) - phys_mem().addr());
      packet_length = static_cast<uint16_t>(desc.len);
    }

    if (desc.has_next) {
      FXL_LOG(ERROR) << "Packet data must be on a single buffer";
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (rx) {
      // Section 5.1.6.4.1 Device Requirements: Processing of Incoming Packets

      // If VIRTIO_NET_F_MRG_RXBUF has not been negotiated, the device MUST
      // set num_buffers to 1.
      header->num_buffers = 1;

      // If none of the VIRTIO_NET_F_GUEST_TSO4, TSO6 or UFO options have been
      // negotiated, the device MUST set gso_type to VIRTIO_NET_HDR_GSO_NONE.
      header->gso_type = VIRTIO_NET_HDR_GSO_NONE;

      // If VIRTIO_NET_F_GUEST_CSUM is not negotiated, the device MUST set
      // flags to zero and SHOULD supply a fully checksummed packet to the
      // driver.
      header->flags = 0;
    }
    entries[num_entries] = {
        .offset = static_cast<uint32_t>(packet_offset),
        .length = static_cast<uint16_t>(packet_length),
        .flags = 0,
        .cookie = reinterpret_cast<void*>(head),
    };
    status = queue->NextAvail(&head);
  }
  if (status != ZX_ERR_SHOULD_WAIT) {
    FXL_LOG(ERROR) << "Failed to fetch descriptor chain from queue";
    return status;
  }

  // Enqueue entries for the Ethernet device.
  uint32_t count;
  status = zx_fifo_write(fifo, entries, sizeof(*entries) * num_entries, &count);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to enqueue buffer";
    return status;
  }
  if (count != num_entries) {
    FXL_LOG(ERROR) << "Only wrote " << count << " of " << num_entries
                   << " entries to Ethernet device";
    return ZX_ERR_IO_DATA_LOSS;
  }

  return ZX_OK;
}

zx_status_t VirtioNet::ReceiveLoop() {
  zx_status_t status;
  do {
    status = DrainQueue(rx_queue(), fifos_.rx_depth, fifos_.rx_fifo, true);
  } while (status == ZX_OK);
  return status;
}

zx_status_t VirtioNet::TransmitLoop() {
  zx_status_t status;
  do {
    status = DrainQueue(tx_queue(), fifos_.tx_depth, fifos_.tx_fifo, false);
  } while (status == ZX_OK);
  return status;
}

}  // namespace machina
