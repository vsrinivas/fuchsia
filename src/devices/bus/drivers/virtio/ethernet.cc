// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethernet.h"

#include <assert.h>
#include <lib/operation/ethernet.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/ethernet.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>
#include <virtio/net.h>
#include <virtio/virtio.h>

#include "ring.h"
#include "trace.h"

// Enables/disables debugging info
#define LOCAL_TRACE 0

namespace virtio {

namespace {

// Specifies how many packets can fit in each of the receive and transmit
// backlogs.
const size_t kBacklog = 32;

// Specifies the maximum transfer unit we support and the maximum layer 1
// Ethernet packet header length.
const size_t kVirtioMtu = 1500;
const size_t kEthHeaderSizeBytes = 14;
const size_t kEthFrameSize = kVirtioMtu + kEthHeaderSizeBytes;

// Other constants determined by the values above and the memory architecture.
// The goal here is to allocate single-page I/O buffers.
const size_t kFrameSize = sizeof(virtio_net_hdr_t) + kEthFrameSize;
const size_t kFramesInBuf = PAGE_SIZE / kFrameSize;
const size_t kNumIoBufs = fbl::round_up(kBacklog * 2, kFramesInBuf) / kFramesInBuf;

const uint16_t kRxId = 0u;
const uint16_t kTxId = 1u;

// Strictly for convenience...
typedef struct vring_desc desc_t;

// I/O buffer helpers
zx_status_t InitBuffers(const zx::bti& bti, std::unique_ptr<io_buffer_t[]>* out) {
  zx_status_t rc;
  fbl::AllocChecker ac;
  std::unique_ptr<io_buffer_t[]> bufs(new (&ac) io_buffer_t[kNumIoBufs]);
  if (!ac.check()) {
    zxlogf(ERROR, "out of memory!\n");
    return ZX_ERR_NO_MEMORY;
  }
  memset(bufs.get(), 0, sizeof(io_buffer_t) * kNumIoBufs);
  size_t buf_size = kFrameSize * kFramesInBuf;
  for (uint16_t id = 0; id < kNumIoBufs; ++id) {
    if ((rc = io_buffer_init(&bufs[id], bti.get(), buf_size, IO_BUFFER_RW | IO_BUFFER_CONTIG)) !=
        ZX_OK) {
      zxlogf(ERROR, "failed to allocate I/O buffers: %s\n", zx_status_get_string(rc));
      return rc;
    }
  }
  *out = std::move(bufs);
  return ZX_OK;
}

void ReleaseBuffers(std::unique_ptr<io_buffer_t[]> bufs) {
  if (!bufs) {
    return;
  }
  for (size_t i = 0; i < kNumIoBufs; ++i) {
    if (io_buffer_is_valid(&bufs[i])) {
      io_buffer_release(&bufs[i]);
    }
  }
}

// Frame access helpers
zx_off_t GetFrame(io_buffer_t** bufs, uint16_t ring_id, uint16_t desc_id) {
  uint16_t i = static_cast<uint16_t>(desc_id + ring_id * kBacklog);
  *bufs = &((*bufs)[i / kFramesInBuf]);
  return (i % kFramesInBuf) * kFrameSize;
}

void* GetFrameVirt(io_buffer_t* bufs, uint16_t ring_id, uint16_t desc_id) {
  zx_off_t offset = GetFrame(&bufs, ring_id, desc_id);
  uintptr_t vaddr = reinterpret_cast<uintptr_t>(io_buffer_virt(bufs));
  return reinterpret_cast<void*>(vaddr + offset);
}

zx_paddr_t GetFramePhys(io_buffer_t* bufs, uint16_t ring_id, uint16_t desc_id) {
  zx_off_t offset = GetFrame(&bufs, ring_id, desc_id);
  return io_buffer_phys(bufs) + offset;
}

virtio_net_hdr_t* GetFrameHdr(io_buffer_t* bufs, uint16_t ring_id, uint16_t desc_id) {
  return reinterpret_cast<virtio_net_hdr_t*>(GetFrameVirt(bufs, ring_id, desc_id));
}

uint8_t* GetFrameData(io_buffer_t* bufs, uint16_t ring_id, uint16_t desc_id, size_t hdr_size) {
  uintptr_t vaddr = reinterpret_cast<uintptr_t>(GetFrameHdr(bufs, ring_id, desc_id));
  return reinterpret_cast<uint8_t*>(vaddr + hdr_size);
}

}  // namespace

zx_status_t EthernetDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;
  if (proto_id == ZX_PROTOCOL_ETHERNET_IMPL) {
    proto->ops = &ethernet_impl_protocol_ops_;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

EthernetDevice::EthernetDevice(zx_device_t* bus_device, zx::bti bti,
                               std::unique_ptr<Backend> backend)
    : virtio::Device(bus_device, std::move(bti), std::move(backend)),
      DeviceType(bus_device),
      rx_(this),
      tx_(this),
      bufs_(nullptr),
      unkicked_(0),
      ifc_({nullptr, nullptr}) {}

EthernetDevice::~EthernetDevice() { LTRACE_ENTRY; }

zx_status_t EthernetDevice::Init() {
  LTRACE_ENTRY;
  zx_status_t rc;
  if (mtx_init(&state_lock_, mtx_plain) != thrd_success ||
      mtx_init(&tx_lock_, mtx_plain) != thrd_success) {
    return ZX_ERR_NO_RESOURCES;
  }
  fbl::AutoLock lock(&state_lock_);

  // Reset the device and read our configuration
  DeviceReset();
  CopyDeviceConfig(&config_, sizeof(config_));
  LTRACEF("mac %02x:%02x:%02x:%02x:%02x:%02x\n", config_.mac[0], config_.mac[1], config_.mac[2],
          config_.mac[3], config_.mac[4], config_.mac[5]);
  LTRACEF("status %u\n", config_.status);
  LTRACEF("max_virtqueue_pairs  %u\n", config_.max_virtqueue_pairs);

  // Ack and set the driver status bit
  DriverStatusAck();

  virtio_hdr_len_ = sizeof(virtio_net_hdr_t);
  if (DeviceFeatureSupported(VIRTIO_F_VERSION_1)) {
    DriverFeatureAck(VIRTIO_F_VERSION_1);
  } else {
    // 5.1.6.1 Legacy Interface: Device Operation
    //
    // The legacy driver only presented num_buffers in the struct
    // virtio_net_hdr when VIRTIO_NET_F_MRG_RXBUF was negotiated; without
    // that feature the structure was 2 bytes shorter.
    virtio_hdr_len_ -= 2;
  }

  // TODO(aarongreen): Check additional features bits and ack/nak them
  rc = DeviceStatusFeaturesOk();
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Feature negotiation failed (%d)\n", tag(), rc);
    return rc;
  }

  // Plan to clean up unless everything goes right.
  auto cleanup = fbl::MakeAutoCall([this]() { DdkRelease(); });

  // Allocate I/O buffers and virtqueues.
  uint16_t num_descs = static_cast<uint16_t>(kBacklog & 0xffff);
  if ((rc = InitBuffers(bti_, &bufs_)) != ZX_OK || (rc = rx_.Init(kRxId, num_descs)) != ZX_OK ||
      (rc = tx_.Init(kTxId, num_descs)) != ZX_OK) {
    zxlogf(ERROR, "failed to allocate virtqueue: %s\n", zx_status_get_string(rc));
    return rc;
  }

  // Associate the I/O buffers with the virtqueue descriptors
  desc_t* desc = nullptr;
  uint16_t id;

  // For rx buffers, we queue a bunch of "reads" from the network that
  // complete when packets arrive.
  for (uint16_t i = 0; i < num_descs; ++i) {
    desc = rx_.AllocDescChain(1, &id);
    desc->addr = GetFramePhys(bufs_.get(), kRxId, id);
    desc->len = kFrameSize;
    desc->flags |= VRING_DESC_F_WRITE;
    LTRACE_DO(virtio_dump_desc(desc));
    rx_.SubmitChain(id);
  }

  // For tx buffers, we hold onto them until we need to send a packet.
  for (uint16_t id = 0; id < num_descs; ++id) {
    desc = tx_.DescFromIndex(id);
    desc->addr = GetFramePhys(bufs_.get(), kTxId, id);
    desc->len = 0;
    desc->flags &= static_cast<uint16_t>(~VRING_DESC_F_WRITE);
    LTRACE_DO(virtio_dump_desc(desc));
  }

  // Start the interrupt thread and set the driver OK status
  StartIrqThread();

  // Initialize the zx_device and publish us
  auto status = DdkAdd("virtio-net");
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to add device: %s\n", zx_status_get_string(rc));
    return status;
  }
  device_ = zxdev();
  // Give the rx buffers to the host
  rx_.Kick();

  // Woohoo! Driver should be ready.
  cleanup.cancel();
  DriverStatusOk();
  return ZX_OK;
}

void EthernetDevice::DdkRelease() {
  LTRACE_ENTRY;
  fbl::AutoLock lock(&state_lock_);
  ReleaseLocked();
}

void EthernetDevice::ReleaseLocked() {
  ifc_.ops = nullptr;
  ReleaseBuffers(std::move(bufs_));
  virtio::Device::Release();
}

void EthernetDevice::IrqRingUpdate() {
  LTRACE_ENTRY;
  // Lock to prevent changes to ifc_.
  {
    fbl::AutoLock lock(&state_lock_);
    if (!ifc_.ops) {
      return;
    }
    // Ring::IrqRingUpdate will call this lambda on each rx buffer filled by
    // the underlying device since the last IRQ.
    // Thread safety analysis is explicitly disabled as clang isn't able to determine that the
    // state_lock_ is  held when the lambda invoked.
    rx_.IrqRingUpdate([this](vring_used_elem* used_elem) TA_NO_THREAD_SAFETY_ANALYSIS {
      uint16_t id = static_cast<uint16_t>(used_elem->id & 0xffff);
      desc_t* desc = rx_.DescFromIndex(id);

      // Transitional driver does not merge rx buffers.
      if ((desc->flags & VRING_DESC_F_NEXT) != 0) {
        zxlogf(ERROR, "dropping rx packet; do not support descriptor chaining");
        while ((desc->flags & VRING_DESC_F_NEXT)) {
          uint16_t next_id = desc->next;
          rx_.FreeDesc(id);
          id = next_id;
          desc = rx_.DescFromIndex(id);
        }
        return;
      }
      assert(used_elem->len <= desc->len);
      uint8_t* data = GetFrameData(bufs_.get(), kRxId, id, virtio_hdr_len_);
      size_t len = used_elem->len - virtio_hdr_len_;
      LTRACEF("Receiving %zu bytes:\n", len);
      LTRACE_DO(hexdump8_ex(data, len, 0));

      // Pass the data up the stack to the generic Ethernet driver
      ethernet_ifc_recv(&ifc_, data, len, 0);
      LTRACE_DO(virtio_dump_desc(desc));
      rx_.FreeDesc(id);
    });
  }

  // Now recycle the rx buffers.  As in Init(), this means queuing a bunch of
  // "reads" from the network that will complete when packets arrive.
  desc_t* desc = nullptr;
  uint16_t id;
  bool need_kick = false;
  while ((desc = rx_.AllocDescChain(1, &id))) {
    desc->len = kFrameSize;
    rx_.SubmitChain(id);
    need_kick = true;
  }

  // If we have re-queued any rx buffers, poke the virtqueue to pick them up.
  if (need_kick) {
    rx_.Kick();
  }
}

void EthernetDevice::IrqConfigChange() {
  LTRACE_ENTRY;
  fbl::AutoLock lock(&state_lock_);
  if (!ifc_.ops) {
    return;
  }

  // Re-read our configuration
  CopyDeviceConfig(&config_, sizeof(config_));
  ethernet_ifc_status(&ifc_, (config_.status & VIRTIO_NET_S_LINK_UP) ? ETHERNET_STATUS_ONLINE : 0);
}

zx_status_t EthernetDevice::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  LTRACE_ENTRY;
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AutoLock lock(&state_lock_);
  if (info) {
    // TODO(aarongreen): Add info->features = GetFeatures();
    info->mtu = kVirtioMtu;
    info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));
    memcpy(info->mac, config_.mac, sizeof(info->mac));
  }
  return ZX_OK;
}

void EthernetDevice::EthernetImplStop() {
  LTRACE_ENTRY;
  fbl::AutoLock lock(&state_lock_);
  ifc_.ops = nullptr;
}

zx_status_t EthernetDevice::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  LTRACE_ENTRY;
  if (!ifc) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AutoLock lock(&state_lock_);
  if (!bufs_ || ifc_.ops) {
    return ZX_ERR_BAD_STATE;
  }
  ifc_ = *ifc;
  ethernet_ifc_status(&ifc_, (config_.status & VIRTIO_NET_S_LINK_UP) ? ETHERNET_STATUS_ONLINE : 0);
  return ZX_OK;
}

void EthernetDevice::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                         ethernet_impl_queue_tx_callback completion_cb,
                                         void* cookie) {
  LTRACE_ENTRY;
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));
  const void* data = op.operation()->data_buffer;
  size_t length = op.operation()->data_size;
  // First, validate the packet
  if (!data || length > kEthFrameSize) {
    zxlogf(ERROR, "dropping packet; invalid packet\n");
    op.Complete(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&tx_lock_);

  // Flush outstanding descriptors.  Ring::IrqRingUpdate will call this lambda
  // on each sent tx_buffer, allowing us to reclaim them.
  auto flush = [this](vring_used_elem* used_elem) {
    uint16_t id = static_cast<uint16_t>(used_elem->id & 0xffff);
    desc_t* desc = tx_.DescFromIndex(id);
    assert((desc->flags & VRING_DESC_F_NEXT) == 0);
    LTRACE_DO(virtio_dump_desc(desc));
    tx_.FreeDesc(id);
  };

  // Grab a free descriptor
  uint16_t id;
  desc_t* desc = tx_.AllocDescChain(1, &id);
  if (!desc) {
    tx_.IrqRingUpdate(flush);
    desc = tx_.AllocDescChain(1, &id);
  }
  if (!desc) {
    zxlogf(ERROR, "dropping packet; out of descriptors\n");
    op.Complete(ZX_ERR_NO_RESOURCES);
    return;
  }

  // Add the data to be sent
  virtio_net_hdr_t* tx_hdr = GetFrameHdr(bufs_.get(), kTxId, id);
  memset(tx_hdr, 0, virtio_hdr_len_);

  // 5.1.6.2.1 Driver Requirements: Packet Transmission
  //
  // The driver MUST set num_buffers to zero.
  //
  // Implementation note: This field doesn't exist if neither
  // |VIRTIO_F_VERSION_1| or |VIRTIO_F_MRG_RXBUF| have been negotiated. Since
  // this field will be part of the payload without these features we elide
  // the check as we know the memory is valid and will soon be overwritten
  // with packet data.
  tx_hdr->num_buffers = 0;

  // If VIRTIO_NET_F_CSUM is not negotiated, the driver MUST set flags to
  // zero and SHOULD supply a fully checksummed packet to the device.
  tx_hdr->flags = 0;

  // If none of the VIRTIO_NET_F_HOST_TSO4, TSO6 or UFO options have been
  // negotiated, the driver MUST set gso_type to VIRTIO_NET_HDR_GSO_NONE.
  tx_hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;

  void* tx_buf = GetFrameData(bufs_.get(), kTxId, id, virtio_hdr_len_);
  memcpy(tx_buf, data, length);
  desc->len = static_cast<uint32_t>(virtio_hdr_len_ + length);

  // Submit the descriptor and notify the back-end.
  LTRACE_DO(virtio_dump_desc(desc));
  LTRACEF("Sending %zu bytes:\n", length);
  LTRACE_DO(hexdump8_ex(tx_buf, length, 0));
  tx_.SubmitChain(id);
  ++unkicked_;
  if ((options & ETHERNET_TX_OPT_MORE) == 0 || unkicked_ > kBacklog / 2) {
    tx_.Kick();
    unkicked_ = 0;
  }
  op.Complete(ZX_OK);
}

}  // namespace virtio
