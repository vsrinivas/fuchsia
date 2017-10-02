// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <zircon/syscalls/port.h>

#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#define QUEUE_SIZE 128u

// Convert guest-physical addresses to usable virtual addresses.
#define guest_paddr_to_host_vaddr(device, addr)                                                    \
    (static_cast<zx_vaddr_t>(((device)->guest_physmem_addr()) + (addr)))

VirtioDevice::VirtioDevice(uint8_t device_id, void* config, size_t config_size,
                           virtio_queue_t* queues, uint16_t num_queues,
                           uintptr_t guest_physmem_addr, size_t guest_physmem_size)
    : device_id_(device_id), device_config_(config), device_config_size_(config_size),
      num_queues_(num_queues), queues_(queues), guest_physmem_addr_(guest_physmem_addr),
      guest_physmem_size_(guest_physmem_size), pci_(this) {
    // Virt queue initialization.
    for (int i = 0; i < num_queues_; ++i) {
        virtio_queue_t* queue = &queues_[i];
        memset(queue, 0, sizeof(*queue));
        queue->size = QUEUE_SIZE;
        queue->virtio_device = this;
    }
}

// Returns a circular index into a Virtio ring.
static uint32_t ring_index(virtio_queue_t* queue, uint32_t index) {
    return index % queue->size;
}

static bool ring_has_avail(virtio_queue_t* queue) {
    if (queue->avail == nullptr)
        return 0;
    return queue->avail->idx != queue->index;
}

static bool validate_queue_range(VirtioDevice* device, zx_vaddr_t addr, size_t size) {
    uintptr_t mem_addr = device->guest_physmem_addr();
    size_t mem_size = device->guest_physmem_size();
    zx_vaddr_t range_end = addr + size;
    zx_vaddr_t mem_end = mem_addr + mem_size;

    return addr >= mem_addr && range_end <= mem_end;
}

template <typename T>
static void queue_set_segment_addr(virtio_queue_t* queue, uint64_t guest_paddr, size_t size,
                                   T** ptr) {
    VirtioDevice* device = queue->virtio_device;
    zx_vaddr_t host_vaddr = guest_paddr_to_host_vaddr(device, guest_paddr);

    *ptr = validate_queue_range(device, host_vaddr, size) ? reinterpret_cast<T*>(host_vaddr)
                                                          : nullptr;
}

void virtio_queue_set_desc_addr(virtio_queue_t* queue, uint64_t desc_paddr) {
    queue->addr.desc = desc_paddr;
    uintptr_t desc_size = queue->size * sizeof(queue->desc[0]);
    queue_set_segment_addr(queue, desc_paddr, desc_size, &queue->desc);
}

void virtio_queue_set_avail_addr(virtio_queue_t* queue, uint64_t avail_paddr) {
    queue->addr.avail = avail_paddr;
    uintptr_t avail_size = sizeof(*queue->avail) + (queue->size * sizeof(queue->avail->ring[0]));
    queue_set_segment_addr(queue, avail_paddr, avail_size, &queue->avail);

    uintptr_t used_event_paddr = avail_paddr + avail_size;
    uintptr_t used_event_size = sizeof(*queue->used_event);
    queue_set_segment_addr(queue, used_event_paddr, used_event_size, &queue->used_event);
}

void virtio_queue_set_used_addr(virtio_queue_t* queue, uint64_t used_paddr) {
    queue->addr.used = used_paddr;
    uintptr_t used_size = sizeof(*queue->used) + (queue->size * sizeof(queue->used->ring[0]));
    queue_set_segment_addr(queue, used_paddr, used_size, &queue->used);

    uintptr_t avail_event_paddr = used_paddr + used_size;
    uintptr_t avail_event_size = sizeof(*queue->avail_event);
    queue_set_segment_addr(queue, avail_event_paddr, avail_event_size, &queue->avail_event);
}

void virtio_queue_signal(virtio_queue_t* queue) {
    mtx_lock(&queue->mutex);
    if (ring_has_avail(queue))
        cnd_signal(&queue->avail_ring_cnd);
    mtx_unlock(&queue->mutex);
}

zx_status_t VirtioDevice::NotifyGuest() {
    return pci_.Interrupt();
}

zx_status_t VirtioDevice::Kick(uint16_t kicked_queue) {
    if (kicked_queue >= num_queues_)
        return ZX_ERR_OUT_OF_RANGE;

    zx_status_t status = HandleQueueNotify(kicked_queue);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to handle queue notify event.\n");
        return status;
    }

    // Send an interrupt back to the guest if we've generated one while
    // processing the queue.
    fbl::AutoLock lock(&mutex_);
    if (isr_status_ > 0) {
        return NotifyGuest();
    }

    // Notify threads waiting on a descriptor.
    virtio_queue_signal(&queues_[kicked_queue]);
    return ZX_OK;
}

// This must not return any errors besides ZX_ERR_NOT_FOUND.
static zx_status_t virtio_queue_next_avail_locked(virtio_queue_t* queue, uint16_t* index) {
    if (!ring_has_avail(queue))
        return ZX_ERR_NOT_FOUND;

    *index = queue->avail->ring[ring_index(queue, queue->index++)];
    return ZX_OK;
}

zx_status_t virtio_queue_next_avail(virtio_queue_t* queue, uint16_t* index) {
    mtx_lock(&queue->mutex);
    zx_status_t status = virtio_queue_next_avail_locked(queue, index);
    mtx_unlock(&queue->mutex);
    return status;
}

void virtio_queue_wait(virtio_queue_t* queue, uint16_t* index) {
    mtx_lock(&queue->mutex);
    while (virtio_queue_next_avail_locked(queue, index) == ZX_ERR_NOT_FOUND)
        cnd_wait(&queue->avail_ring_cnd, &queue->mutex);
    mtx_unlock(&queue->mutex);
}

struct poll_task_args_t {
    virtio_queue_t* queue;
    virtio_queue_poll_fn_t handler;
    void* ctx;

    poll_task_args_t(virtio_queue_t* queue, virtio_queue_poll_fn_t handler, void* ctx)
        : queue(queue), handler(handler), ctx(ctx) {}
};

static int virtio_queue_poll_task(void* ctx) {
    zx_status_t result = ZX_OK;
    fbl::unique_ptr<poll_task_args_t> args(static_cast<poll_task_args_t*>(ctx));
    while (true) {
        uint16_t descriptor;
        virtio_queue_wait(args->queue, &descriptor);

        uint32_t used = 0;
        zx_status_t status = args->handler(args->queue, descriptor, &used, args->ctx);
        virtio_queue_return(args->queue, descriptor, used);

        if (status == ZX_ERR_STOP)
            break;
        if (status != ZX_OK) {
            fprintf(stderr, "Error %d while handling queue buffer.\n", status);
            result = status;
            break;
        }

        result = args->queue->virtio_device->NotifyGuest();
        if (result != ZX_OK)
            break;
    }

    return result;
}

zx_status_t virtio_queue_poll(virtio_queue_t* queue, virtio_queue_poll_fn_t handler, void* ctx) {
    auto args = fbl::make_unique<poll_task_args_t>(queue, handler, ctx);

    thrd_t thread;
    int ret = thrd_create(&thread, virtio_queue_poll_task, args.release());
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create queue thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }

    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach queue thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t virtio_queue_read_desc(virtio_queue_t* queue, uint16_t desc_index, virtio_desc_t* out) {
    VirtioDevice* device = queue->virtio_device;
    volatile struct vring_desc& desc = queue->desc[desc_index];
    size_t mem_size = device->guest_physmem_size();

    const uint64_t end = desc.addr + desc.len;
    if (end < desc.addr || end > mem_size)
        return ZX_ERR_OUT_OF_RANGE;

    out->addr = reinterpret_cast<void*>(guest_paddr_to_host_vaddr(device, desc.addr));
    out->len = desc.len;
    out->has_next = desc.flags & VRING_DESC_F_NEXT;
    out->writable = desc.flags & VRING_DESC_F_WRITE;
    out->next = desc.next;
    return ZX_OK;
}

void virtio_queue_return(virtio_queue_t* queue, uint16_t index, uint32_t len) {
    mtx_lock(&queue->mutex);

    volatile struct vring_used_elem* used = &queue->used->ring[ring_index(queue, queue->used->idx)];

    used->id = index;
    used->len = len;
    queue->used->idx++;

    mtx_unlock(&queue->mutex);

    // Set the queue bit in the device ISR so that the driver knows to check
    // the queues on the next interrupt.
    queue->virtio_device->add_isr_flags(VirtioDevice::VIRTIO_ISR_QUEUE);
}

zx_status_t virtio_queue_handler(virtio_queue_t* queue, virtio_queue_fn_t handler, void* context) {
    uint16_t head;
    uint32_t used_len = 0;
    uintptr_t mem_addr = queue->virtio_device->guest_physmem_addr();
    size_t mem_size = queue->virtio_device->guest_physmem_size();

    // Get the next descriptor from the available ring. If none are available
    // we can just no-op.
    zx_status_t status = virtio_queue_next_avail(queue, &head);
    if (status == ZX_ERR_NOT_FOUND)
        return ZX_OK;
    if (status != ZX_OK)
        return status;

    status = ZX_OK;
    uint16_t desc_index = head;
    volatile const struct vring_desc* desc;
    do {
        if (desc_index >= queue->size)
            return ZX_ERR_OUT_OF_RANGE;
        desc = &queue->desc[desc_index];

        const uint64_t end = desc->addr + desc->len;
        if (end < desc->addr || end > mem_size)
            return ZX_ERR_OUT_OF_RANGE;

        void* addr = reinterpret_cast<void*>(mem_addr + desc->addr);
        status = handler(addr, desc->len, desc->flags, &used_len, context);
        if (status != ZX_OK) {
            fprintf(stderr, "Virtio request (%#lx, %u) failed %d\n", desc->addr, desc->len, status);
            return status;
        }

        desc_index = desc->next;
    } while (desc->flags & VRING_DESC_F_NEXT);

    virtio_queue_return(queue, head, used_len);

    return ring_has_avail(queue) ? ZX_ERR_NEXT : ZX_OK;
}

zx_status_t VirtioDevice::ReadConfig(uint16_t port, uint8_t access_size, zx_vcpu_io_t* vcpu_io) {
    fbl::AutoLock lock(&config_mutex_);
    vcpu_io->access_size = access_size;
    switch (access_size) {
    case 1: {
        uint8_t* buf = reinterpret_cast<uint8_t*>(device_config_);
        vcpu_io->u8 = buf[port];
        return ZX_OK;
    }
    case 2: {
        uint16_t* buf = reinterpret_cast<uint16_t*>(device_config_);
        vcpu_io->u16 = buf[port / 2];
        return ZX_OK;
    }
    case 4: {
        uint32_t* buf = reinterpret_cast<uint32_t*>(device_config_);
        vcpu_io->u32 = buf[port / 4];
        return ZX_OK;
    }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioDevice::WriteConfig(uint16_t port, const zx_vcpu_io_t* io) {
    fbl::AutoLock lock(&config_mutex_);
    switch (io->access_size) {
    case 1: {
        uint8_t* buf = reinterpret_cast<uint8_t*>(device_config_);
        buf[port] = io->u8;
        return ZX_OK;
    }
    case 2: {
        uint16_t* buf = reinterpret_cast<uint16_t*>(device_config_);
        buf[port / 2] = io->u16;
        return ZX_OK;
    }
    case 4: {
        uint32_t* buf = reinterpret_cast<uint32_t*>(device_config_);
        buf[port / 4] = io->u32;
        return ZX_OK;
    }
    }
    return ZX_ERR_NOT_SUPPORTED;
}
