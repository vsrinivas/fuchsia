// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <string.h>
#include <virtio/virtio.h>
#include <lib/zx/vmar.h>

#define LOCAL_TRACE 0

namespace virtio {

namespace {

zx_status_t QueueTransfer(Ring* ring, uintptr_t phys, uint32_t len, bool write) {
    uint16_t index;
    vring_desc* desc = ring->AllocDescChain(1, &index);
    if (!desc) {
        // This should not happen
        zxlogf(ERROR, "Failed to find free descriptor for the virtio ring\n");
        return ZX_ERR_NO_MEMORY;
    }

    desc->addr = phys;
    desc->len = len;
    // writeable for the driver is readonly for the device and vice versa
    desc->flags = write ? 0 : VRING_DESC_F_WRITE;
    ring->SubmitChain(index);

    return ZX_OK;
}

} // namespace

TransferBuffer::TransferBuffer() {
    memset(&buf_, 0, sizeof(buf_));
}

TransferBuffer::~TransferBuffer() {
    io_buffer_release(&buf_);
}

zx_status_t TransferBuffer::Init(const zx::bti& bti, size_t count, uint32_t chunk_size) {
    if (!count)
        return ZX_OK;

    count_ = count;
    chunk_size_ = chunk_size;
    size_ = count * chunk_size;

    TransferDescriptor* descriptor = new TransferDescriptor[count_];
    if (!descriptor) {
        zxlogf(ERROR, "Failed to allocate transfer descriptors (%d)\n", ZX_ERR_NO_MEMORY);
        return ZX_ERR_NO_MEMORY;
    }

    descriptor_.reset(descriptor, count_);

    zx_status_t status = io_buffer_init(&buf_, bti.get(), size_, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to allocate transfer buffers (%d)\n", status);
        return status;
    }

    void* virt = io_buffer_virt(&buf_);
    zx_paddr_t phys = io_buffer_phys(&buf_);
    for (size_t i = 0; i < count_; ++i) {
        TransferDescriptor& desc = descriptor_[i];

        desc.virt = reinterpret_cast<uint8_t*>(virt) + i * chunk_size;
        desc.phys = phys + i * chunk_size;
        desc.total_len = chunk_size;
        desc.used_len = 0;
        desc.processed_len = 0;
    }

    return ZX_OK;
}

TransferDescriptor* TransferBuffer::GetDescriptor(size_t index) {
    if (index > count_)
        return nullptr;
    return &descriptor_[index];
}

TransferDescriptor* TransferBuffer::PhysicalToDescriptor(uintptr_t phys) {
    zx_paddr_t base = io_buffer_phys(&buf_);
    if (phys < base || phys >= base + size_)
        return nullptr;
    return &descriptor_[(phys - base) / chunk_size_];
}

void TransferQueue::Add(TransferDescriptor* desc) {
    queue_.push_front(desc);
}

TransferDescriptor* TransferQueue::Peek() {
    if (queue_.is_empty())
        return nullptr;
    return &queue_.back();
}

TransferDescriptor* TransferQueue::Dequeue() {
    if (queue_.is_empty())
        return nullptr;
    return queue_.pop_back();
}

bool TransferQueue::IsEmpty() const {
    return queue_.is_empty();
}

ConsoleDevice::ConsoleDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {}

ConsoleDevice::~ConsoleDevice() {}

// We don't need to hold request_lock_ during initialization
zx_status_t ConsoleDevice::Init() TA_NO_THREAD_SAFETY_ANALYSIS {
    LTRACE_ENTRY;
    // It's a common part for all virtio devices: reset the device, notify
    // about the driver and negotiate supported features
    DeviceReset();
    DriverStatusAck();
    if (!DeviceFeatureSupported(VIRTIO_F_VERSION_1)) {
        zxlogf(ERROR, "%s: Legacy virtio interface is not supported by this driver\n", tag());
        return ZX_ERR_NOT_SUPPORTED;
    }
    DriverFeatureAck(VIRTIO_F_VERSION_1);

    zx_status_t status = DeviceStatusFeaturesOk();
    if (status) {
        zxlogf(ERROR, "%s: Feature negotiation failed (%d)\n", tag(), status);
        return status;
    }

    status = port0_receive_queue_.Init(0, kDescriptors);
    if (status) {
        zxlogf(ERROR, "%s: Failed to initialize receive queue (%d)\n", tag(), status);
        return status;
    }

    status = port0_receive_buffer_.Init(bti_, kDescriptors, kChunkSize);
    if (status) {
        zxlogf(ERROR, "%s: Failed to allocate buffers for receive queue (%d)\n", tag(), status);
        return status;
    }

    // Initially the whole receive buffer is available for device to write, so
    // put all descriptors in the virtio ring available list
    for (size_t i = 0; i < kDescriptors; ++i) {
        TransferDescriptor* desc = port0_receive_buffer_.GetDescriptor(i);
        QueueTransfer(&port0_receive_queue_, desc->phys, desc->total_len, /*write*/ 0);
    }
    // Notify the device
    port0_receive_queue_.Kick();

    status = port0_transmit_queue_.Init(1, kDescriptors);
    if (status) {
        zxlogf(ERROR, "%s: Failed to initialize transmit queue (%d)\n", tag(), status);
        return status;
    }

    status = port0_transmit_buffer_.Init(bti_, kDescriptors, kChunkSize);
    if (status) {
        zxlogf(ERROR, "%s: Failed to allocate buffers for transmit queue (%d)\n", tag(), status);
        return status;
    }

    // Initially the whole transmit buffer available for writing, so put all the
    // descriptors in the queue
    for (size_t i = 0; i < kDescriptors; ++i) {
        TransferDescriptor* desc = port0_transmit_buffer_.GetDescriptor(i);
        port0_transmit_descriptors_.Add(desc);
    }

    device_ops_.read = virtio_console_read;
    device_ops_.write = virtio_console_write;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-console";
    args.ctx = this;
    args.ops = &device_ops_;

    // We probably want to have an alias for console devices
    args.proto_id = ZX_PROTOCOL_CONSOLE;

    status = device_add(bus_device_, &args, &device_);
    if (status) {
        zxlogf(ERROR, "%s: Failed to register device (%d)\n", tag(), status);
        device_ = nullptr;
        return status;
    }

    StartIrqThread();
    DriverStatusOk();

    LTRACE_EXIT;
    return ZX_OK;
}

void ConsoleDevice::IrqRingUpdate() {
    LTRACE_ENTRY;

    fbl::AutoLock a(&request_lock_);

    // These callbacks are called synchronously, so we don't need to acquire request_lock_
    port0_receive_queue_.IrqRingUpdate([this](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
        uint16_t index = static_cast<uint16_t>(elem->id);
        vring_desc* desc = port0_receive_queue_.DescFromIndex(index);
        uint32_t remain = elem->len;

        for (;;) {
            bool has_next = desc->flags & VRING_DESC_F_NEXT;
            uint16_t next = desc->next;

            TransferDescriptor* trans = port0_receive_buffer_.PhysicalToDescriptor(desc->addr);

            trans->processed_len = 0;
            trans->used_len = fbl::min(trans->total_len, remain);
            remain -= trans->used_len;
            port0_receive_descriptors_.Add(trans);

            port0_receive_queue_.FreeDesc(index);
            if (!has_next)
                break;

            index = next;
            desc = port0_receive_queue_.DescFromIndex(index);
        }
        device_state_set(device_, DEV_STATE_READABLE);
    });

    port0_transmit_queue_.IrqRingUpdate([this](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
        uint16_t index = static_cast<uint16_t>(elem->id);
        vring_desc* desc = port0_transmit_queue_.DescFromIndex(index);

        for (;;) {
            bool has_next = desc->flags & VRING_DESC_F_NEXT;
            uint16_t next = desc->next;

            TransferDescriptor* trans = port0_transmit_buffer_.PhysicalToDescriptor(desc->addr);

            port0_transmit_descriptors_.Add(trans);

            port0_transmit_queue_.FreeDesc(index);
            if (!has_next)
                break;

            index = next;
            desc = port0_transmit_queue_.DescFromIndex(index);
        }
        device_state_set(device_, DEV_STATE_WRITABLE);
    });
    LTRACE_EXIT;
}

zx_status_t ConsoleDevice::virtio_console_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    ConsoleDevice* console = reinterpret_cast<ConsoleDevice*>(ctx);

    return console->Read(buf, count, off, actual);
}

zx_status_t ConsoleDevice::Read(void* buf, size_t count, zx_off_t off, size_t* actual) {
    LTRACE_ENTRY;
    *actual = 0;

    if (count > UINT32_MAX)
        count = UINT32_MAX;

    fbl::AutoLock a(&request_lock_);

    TransferDescriptor* desc = port0_receive_descriptors_.Peek();
    if (!desc) {
        device_state_clr(device_, DEV_STATE_READABLE);
        return ZX_ERR_SHOULD_WAIT;
    }

    uint32_t len = fbl::min(static_cast<uint32_t>(count), desc->used_len - desc->processed_len);
    memcpy(buf, desc->virt + desc->processed_len, len);
    desc->processed_len += len;
    *actual += len;

    // Did we read the whole buffer? If so return it back to the device
    if (desc->processed_len == desc->used_len) {
        port0_receive_descriptors_.Dequeue();
        QueueTransfer(&port0_receive_queue_, desc->phys, desc->total_len, /*write*/ 0);
        port0_receive_queue_.Kick();
    }

    LTRACE_EXIT;
    return ZX_OK;
}

zx_status_t ConsoleDevice::virtio_console_write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
    ConsoleDevice* console = reinterpret_cast<ConsoleDevice*>(ctx);

    return console->Write(buf, count, off, actual);
}

zx_status_t ConsoleDevice::Write(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    LTRACE_ENTRY;
    *actual = 0;

    if (count > UINT32_MAX)
        count = UINT32_MAX;

    fbl::AutoLock a(&request_lock_);

    TransferDescriptor* desc = port0_transmit_descriptors_.Dequeue();
    if (!desc) {
        device_state_clr(device_, DEV_STATE_WRITABLE);
        return ZX_ERR_SHOULD_WAIT;
    }

    uint32_t len = fbl::min(static_cast<uint32_t>(count), desc->total_len);
    memcpy(desc->virt, buf, len);
    desc->used_len = len;
    *actual += len;

    QueueTransfer(&port0_transmit_queue_, desc->phys, desc->used_len, /*write*/ 1);
    port0_transmit_queue_.Kick();

    LTRACE_EXIT;
    return ZX_OK;
}

} // namespace virtio
