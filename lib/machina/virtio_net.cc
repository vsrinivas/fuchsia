// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_net.h"

#include <fcntl.h>
#include <string.h>
#include <atomic>

#include <trace-engine/types.h>
#include <trace/event.h>
#include <zircon/device/ethernet.h>
#include <zx/fifo.h>

#include "lib/fxl/logging.h"

namespace machina {

VirtioNet::Stream::Stream(VirtioNet* device, async_dispatcher_t* dispatcher, VirtioQueue* queue,
                          std::atomic<trace_async_id_t>* trace_flow_id)
    : device_(device),
      dispatcher_(dispatcher),
      queue_(queue),
      trace_flow_id_(trace_flow_id),
      queue_wait_(dispatcher, queue,
                  fit::bind_member(this, &VirtioNet::Stream::OnQueueReady)) {}

zx_status_t VirtioNet::Stream::Start(zx_handle_t fifo, size_t fifo_max_entries,
                                     bool rx) {
  fifo_ = fifo;
  rx_ = rx;
  eth_fifo_entry_t* entries = new eth_fifo_entry_t[fifo_max_entries];
  fifo_entries_.reset(entries, fifo_max_entries);
  fifo_num_entries_ = 0;
  fifo_entries_write_index_ = 0;

  fifo_readable_wait_.set_object(fifo_);
  fifo_readable_wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
  fifo_writable_wait_.set_object(fifo_);
  fifo_writable_wait_.set_trigger(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED);

  // One async job will pipe buffers from the queue into the FIFO.
  zx_status_t status = WaitOnQueue();
  if (status == ZX_OK) {
    // A second async job will return buffers from the FIFO to the queue.
    status = WaitOnFifoReadable();
  }
  return status;
}

zx_status_t VirtioNet::Stream::WaitOnQueue() { return queue_wait_.Begin(); }

void VirtioNet::Stream::OnQueueReady(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  // Attempt to correlate the processing of descriptors with a previous kick.
  // As noted in virtio_device.cc this should be considered best-effort only.
  const trace_async_id_t flow_id = trace_flow_id_->load();
  TRACE_DURATION("machina", "virtio_net_packet_read_from_queue", "direction",
                 TA_STRING_LITERAL(rx_ ? "RX" : "TX"), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_STEP("machina", "io_queue_signal", flow_id);
  }

  FXL_DCHECK(fifo_num_entries_ == 0);
  virtio_desc_t desc;
  fifo_num_entries_ = 0;
  fifo_entries_write_index_ = 0;
  do {
    status = queue_->ReadDesc(index, &desc);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to read descriptor from queue";
      return;
    }

    uintptr_t packet_offset;
    uintptr_t packet_length;
    auto header = reinterpret_cast<virtio_net_hdr_t*>(desc.addr);
    if (!desc.has_next) {
      packet_offset = device_->phys_mem().offset(header + 1);
      packet_length = static_cast<uint16_t>(desc.len - sizeof(*header));
    } else if (desc.len == sizeof(virtio_net_hdr_t)) {
      status = queue_->ReadDesc(desc.next, &desc);
      packet_offset = device_->phys_mem().offset(desc.addr, desc.len);
      packet_length = static_cast<uint16_t>(desc.len);
    }

    if (desc.has_next) {
      FXL_LOG(ERROR) << "Packet data must be on a single buffer";
      return;
    }
    if (rx_) {
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

    FXL_DCHECK(fifo_num_entries_ < fifo_entries_.size());
    fifo_entries_[fifo_num_entries_++] = {
        .offset = static_cast<uint32_t>(packet_offset),
        .length = static_cast<uint16_t>(packet_length),
        .flags = 0,
        .cookie = reinterpret_cast<void*>(index),
    };
  } while (fifo_num_entries_ < fifo_entries_.size() &&
           queue_->NextAvail(&index) == ZX_OK);

  status = WaitOnFifoWritable();
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Failed to wait on fifo writable: " << status;
  }
}

zx_status_t VirtioNet::Stream::WaitOnFifoWritable() {
  return fifo_writable_wait_.Begin(dispatcher_);
}

void VirtioNet::Stream::OnFifoWritable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Async wait failed on fifo writable: " << status;
    return;
  }

  // Attempt to correlate the processing of packets with an existing flow.
  const trace_async_id_t flow_id = trace_flow_id_->load();
  TRACE_DURATION("machina", "virtio_net_packet_pipe_to_fifo", "direction",
                 TA_STRING_LITERAL(rx_ ? "RX" : "TX"), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_STEP("machina", "io_queue_signal", flow_id);
  }

  size_t num_entries_written = 0;
  status = zx_fifo_write(
      fifo_, sizeof(fifo_entries_[0]),
      static_cast<const void*>(&fifo_entries_[fifo_entries_write_index_]),
      fifo_num_entries_, &num_entries_written);
  fifo_entries_write_index_ += num_entries_written;
  fifo_num_entries_ -= num_entries_written;
  if (status == ZX_ERR_SHOULD_WAIT ||
      (status == ZX_OK && fifo_num_entries_ > 0)) {
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Async wait failed on fifo writable: " << status;
    }
    return;
  }
  if (status == ZX_OK) {
    status = WaitOnQueue();
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed write entries to fifo: " << status;
  }
}

zx_status_t VirtioNet::Stream::WaitOnFifoReadable() {
  return fifo_readable_wait_.Begin(dispatcher_);
}

void VirtioNet::Stream::OnFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Async wait failed on fifo readable: " << status;
    return;
  }

  // Attempt to correlate the processing of packets with an existing flow.
  const trace_async_id_t flow_id = trace_flow_id_->exchange(0);
  TRACE_DURATION("machina", "virtio_net_packet_return_to_queue", "direction",
                 TA_STRING_LITERAL(rx_ ? "RX" : "TX"), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_END("machina", "io_queue_signal", flow_id);
  }

  // Dequeue entries for the Ethernet device.
  size_t num_entries_read;
  eth_fifo_entry_t entries[fifo_entries_.size()];
  status = zx_fifo_read(fifo_, sizeof(entries[0]), entries, countof(entries),
                        &num_entries_read);
  if (status == ZX_ERR_SHOULD_WAIT) {
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Async wait failed on fifo readable: " << status;
    }
    return;
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read from fifo: " << status;
    return;
  }

  for (size_t i = 0; i < num_entries_read; i++) {
    auto head = reinterpret_cast<uintptr_t>(entries[i].cookie);
    auto length = entries[i].length + sizeof(virtio_net_hdr_t);
    status = queue_->Return(head, length);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to return descriptor to the queue " << status;
      return;
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Async wait failed on fifo readable: " << status;
  }
}

VirtioNet::VirtioNet(const PhysMem& phys_mem, async_dispatcher_t* dispatcher)
    : VirtioDeviceBase(phys_mem),
      rx_stream_(this, dispatcher, rx_queue(), rx_trace_flow_id()),
      tx_stream_(this, dispatcher, tx_queue(), tx_trace_flow_id()) {
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

  FXL_LOG(INFO) << "Polling device " << path << " for Ethernet frames";
  return WaitOnFifos(fifos_);
}

zx_status_t VirtioNet::WaitOnFifos(const eth_fifos_t& fifos) {
  zx_status_t status = rx_stream_.Start(fifos.rx_fifo, fifos.rx_depth, true);
  if (status == ZX_OK) {
    status = tx_stream_.Start(fifos.tx_fifo, fifos.tx_depth, false);
  }
  return status;
}

}  // namespace machina
