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

VirtioNet::VirtioNet(const PhysMem& phys_mem)
    : VirtioDevice(VIRTIO_ID_NET,
                   &config_,
                   sizeof(config_),
                   queues_,
                   kNumQueues,
                   phys_mem) {
  config_.status = VIRTIO_NET_S_LINK_UP;
  config_.max_virtqueue_pairs = 1;
  // TODO(abdulla): Support VIRTIO_NET_F_STATUS via IOCTL_ETHERNET_GET_STATUS.
  add_device_features(VIRTIO_NET_F_MAC);
}

VirtioNet::~VirtioNet() {
  zx_handle_close(fifos_.tx_fifo);
  zx_handle_close(fifos_.rx_fifo);
}

zx_status_t VirtioNet::Start(const char* path) {
  net_fd_.reset(open(path, O_RDONLY));
  if (!net_fd_) {
    FXL_LOG(INFO) << "Could not open Ethernet device " << path;
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
      zx_handle_duplicate(phys_mem().vmo(), ZX_RIGHT_SAME_RIGHTS, &vmo);
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

  FXL_LOG(INFO) << "Polling device " << path << " for Ethernet frames";
  return ZX_ERR_STOP;
}

zx_status_t VirtioNet::DrainQueue(virtio_queue_t* queue,
                                  uint32_t max_entries,
                                  zx_handle_t fifo) {
  eth_fifo_entry_t entries[max_entries];

  // Wait on first descriptor chain to become available.
  uint16_t head;
  virtio_queue_wait(queue, &head);

  // Read all available descriptor chains from the queue.
  zx_status_t status = ZX_OK;
  uint32_t num_entries = 0;
  for (; num_entries < max_entries && status == ZX_OK; num_entries++) {
    virtio_desc_t desc;
    status = virtio_queue_read_desc(queue, head, &desc);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to read descriptor from queue";
      return status;
    }
    if (desc.has_next) {
      FXL_LOG(ERROR) << "Descriptor chain must have a length of 1";
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    auto header = reinterpret_cast<virtio_net_hdr_t*>(desc.addr);
    entries[num_entries] = {
        .offset = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(header + 1) - phys_mem().addr()),
        .length = static_cast<uint16_t>(desc.len - sizeof(*header)),
        .flags = 0,
        .cookie = reinterpret_cast<void*>(head),
    };
    status = virtio_queue_next_avail(queue, &head);
  }
  if (status != ZX_ERR_NOT_FOUND) {
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

  // Wait for entries to dequeue.
  while (true) {
    zx_signals_t observed;
    status = zx_object_wait_one(fifo, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                ZX_TIME_INFINITE, &observed);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait on queue";
      return status;
    }
    if (observed & ZX_FIFO_PEER_CLOSED) {
      FXL_LOG(ERROR) << "FIFO was closed";
      return status;
    }
    if (observed & ZX_FIFO_READABLE) {
      break;
    }
  }

  // Dequeue entries for the Ethernet device.
  status = zx_fifo_read(fifo, entries, sizeof(entries), &count);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read from queue";
  }
  for (uint32_t i = 0; i < count; i++) {
    auto head = reinterpret_cast<uintptr_t>(entries[i].cookie);
    auto length = entries[i].length + sizeof(virtio_net_hdr_t);
    virtio_queue_return(rx_queue(), head, length);
  }

  // Notify guest of updates to the queue.
  NotifyGuest();
  return ZX_OK;
}

zx_status_t VirtioNet::ReceiveLoop() {
  zx_status_t status;
  do {
    status = DrainQueue(rx_queue(), fifos_.rx_depth, fifos_.rx_fifo);
  } while (status == ZX_OK);
  return status;
}

zx_status_t VirtioNet::TransmitLoop() {
  zx_status_t status;
  do {
    status = DrainQueue(tx_queue(), fifos_.tx_depth, fifos_.tx_fifo);
  } while (status == ZX_OK);
  return status;
}

}  // namespace machina
