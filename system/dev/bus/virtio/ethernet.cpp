// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethernet.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/ethernet.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <pretty/hexdump.h>
#include <virtio/net.h>
#include <virtio/virtio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

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
const size_t kL1EthHdrLen = 26;

// Other constants determined by the values above and the memory architecture.
// The goal here is to allocate single-page I/O buffers.
const size_t kFrameSize = sizeof(virtio_net_hdr_t) + kL1EthHdrLen + kVirtioMtu;
const size_t kFramesInBuf = PAGE_SIZE / kFrameSize;
const size_t kNumIoBufs = fbl::round_up(kBacklog * 2, kFramesInBuf) / kFramesInBuf;

const uint16_t kRxId = 0u;
const uint16_t kTxId = 1u;

// Strictly for convenience...
typedef struct vring_desc desc_t;

// Device bridge helpers
void virtio_net_unbind(void* ctx) {
    virtio::EthernetDevice* eth = static_cast<virtio::EthernetDevice*>(ctx);
    eth->Unbind();
}

void virtio_net_release(void* ctx) {
    fbl::unique_ptr<virtio::EthernetDevice> eth(static_cast<virtio::EthernetDevice*>(ctx));
    eth->Release();
}

zx_protocol_device_t kDeviceOps = {
    DEVICE_OPS_VERSION,
    nullptr, // get_protocol
    nullptr, // open
    nullptr, // openat
    nullptr, // close
    virtio_net_unbind,
    virtio_net_release,
    nullptr, // read
    nullptr, // write
    nullptr, // get_size
    nullptr, // ioctl
    nullptr, // suspend
    nullptr, // resume
    nullptr, // rxrpc
};

// Protocol bridge helpers
zx_status_t virtio_net_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    virtio::EthernetDevice* eth = static_cast<virtio::EthernetDevice*>(ctx);
    return eth->Query(options, info);
}

void virtio_net_stop(void* ctx) {
    virtio::EthernetDevice* eth = static_cast<virtio::EthernetDevice*>(ctx);
    eth->Stop();
}

zx_status_t virtio_net_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    virtio::EthernetDevice* eth = static_cast<virtio::EthernetDevice*>(ctx);
    return eth->Start(ifc, cookie);
}

zx_status_t virtio_net_queue_tx(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) {
    virtio::EthernetDevice* eth = static_cast<virtio::EthernetDevice*>(ctx);
    return eth->QueueTx(options, netbuf);
}

static zx_status_t virtio_set_param(void* ctx, uint32_t param, int32_t value, void* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

ethmac_protocol_ops_t kProtoOps = {
    virtio_net_query,
    virtio_net_stop,
    virtio_net_start,
    virtio_net_queue_tx,
    virtio_set_param,
    NULL, // get_bti not implemented because we don't have FEATURE_DMA
};

// I/O buffer helpers
zx_status_t InitBuffers(const zx::bti& bti, fbl::unique_ptr<io_buffer_t[]>* out) {
    zx_status_t rc;
    fbl::AllocChecker ac;
    fbl::unique_ptr<io_buffer_t[]> bufs(new (&ac) io_buffer_t[kNumIoBufs]);
    if (!ac.check()) {
        zxlogf(ERROR, "out of memory!\n");
        return ZX_ERR_NO_MEMORY;
    }
    memset(bufs.get(), 0, sizeof(io_buffer_t) * kNumIoBufs);
    size_t buf_size = kFrameSize * kFramesInBuf;
    for (uint16_t id = 0; id < kNumIoBufs; ++id) {
        if ((rc = io_buffer_init(&bufs[id], bti.get(), buf_size,
                                 IO_BUFFER_RW | IO_BUFFER_CONTIG)) != ZX_OK) {
            zxlogf(ERROR, "failed to allocate I/O buffers: %s\n", zx_status_get_string(rc));
            return rc;
        }
    }
    *out = fbl::move(bufs);
    return ZX_OK;
}

void ReleaseBuffers(fbl::unique_ptr<io_buffer_t[]> bufs) {
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

} // namespace

EthernetDevice::EthernetDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)), rx_(this), tx_(this), bufs_(nullptr),
      unkicked_(0), ifc_(nullptr), cookie_(nullptr) {
}

EthernetDevice::~EthernetDevice() {
    LTRACE_ENTRY;
}

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
    auto cleanup = fbl::MakeAutoCall([this]() { Release(); });

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
    device_add_args_t args;
    memset(&args, 0, sizeof(args));
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-net";
    args.ctx = this;
    args.ops = &kDeviceOps;
    args.proto_id = ZX_PROTOCOL_ETHERNET_IMPL;
    args.proto_ops = &kProtoOps;
    if ((rc = device_add(bus_device_, &args, &device_)) != ZX_OK) {
        zxlogf(ERROR, "failed to add device: %s\n", zx_status_get_string(rc));
        return rc;
    }
    // Give the rx buffers to the host
    rx_.Kick();

    // Woohoo! Driver should be ready.
    cleanup.cancel();
    DriverStatusOk();
    return ZX_OK;
}

void EthernetDevice::Release() {
    LTRACE_ENTRY;
    fbl::AutoLock lock(&state_lock_);
    ReleaseLocked();
}

void EthernetDevice::ReleaseLocked() {
    ifc_ = nullptr;
    ReleaseBuffers(fbl::move(bufs_));
    Device::Release();
}

void EthernetDevice::IrqRingUpdate() {
    LTRACE_ENTRY;
    // Lock to prevent changes to ifc_.
    {
        fbl::AutoLock lock(&state_lock_);
        if (!ifc_) {
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
            assert(used_elem->len < desc->len);
            uint8_t* data = GetFrameData(bufs_.get(), kRxId, id, virtio_hdr_len_);
            size_t len = used_elem->len - virtio_hdr_len_;
            LTRACEF("Receiving %zu bytes:\n", len);
            LTRACE_DO(hexdump8_ex(data, len, 0));

            // Pass the data up the stack to the generic Ethernet driver
            ifc_->recv(cookie_, data, len, 0);
            assert((desc->flags & VRING_DESC_F_NEXT) == 0);
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
    if (!ifc_) {
        return;
    }

    // Re-read our configuration
    CopyDeviceConfig(&config_, sizeof(config_));
    ifc_->status(cookie_, (config_.status & VIRTIO_NET_S_LINK_UP) ? ETH_STATUS_ONLINE : 0);
}

zx_status_t EthernetDevice::Query(uint32_t options, ethmac_info_t* info) {
    LTRACE_ENTRY;
    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AutoLock lock(&state_lock_);
    if (info) {
        // TODO(aarongreen): Add info->features = GetFeatures();
        info->mtu = kVirtioMtu;
        memcpy(info->mac, config_.mac, sizeof(info->mac));
    }
    return ZX_OK;
}

void EthernetDevice::Stop() {
    LTRACE_ENTRY;
    fbl::AutoLock lock(&state_lock_);
    ifc_ = nullptr;
}

zx_status_t EthernetDevice::Start(ethmac_ifc_t* ifc, void* cookie) {
    LTRACE_ENTRY;
    if (!ifc) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AutoLock lock(&state_lock_);
    if (!bufs_ || ifc_) {
        return ZX_ERR_BAD_STATE;
    }
    ifc_ = ifc;
    cookie_ = cookie;
    ifc_->status(cookie_, (config_.status & VIRTIO_NET_S_LINK_UP) ? ETH_STATUS_ONLINE : 0);
    return ZX_OK;
}

zx_status_t EthernetDevice::QueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
    LTRACE_ENTRY;
    void* data = netbuf->data;
    size_t length = netbuf->len;
    // First, validate the packet
    if (!data || length > virtio_hdr_len_ + kVirtioMtu) {
        LTRACEF("dropping packet; invalid packet\n");
        return ZX_ERR_INVALID_ARGS;
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
        LTRACEF("dropping packet; out of descriptors\n");
        return ZX_ERR_NO_RESOURCES;
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
    if ((options & ETHMAC_TX_OPT_MORE) == 0 || unkicked_ > kBacklog / 2) {
        tx_.Kick();
        unkicked_ = 0;
    }
    return ZX_OK;
}

} // namespace virtio
