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
#include <magenta/syscalls/port.h>

#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

// Convert guest-physical addresses to usable virtual addresses.
#define guest_paddr_to_host_vaddr(device, addr) \
    (static_cast<mx_vaddr_t>(((device)->guest_physmem_addr) + (addr)))

// Returns a circular index into a Virtio ring.
static uint32_t ring_index(virtio_queue_t* queue, uint32_t index) {
    return index % queue->size;
}

static int ring_avail_count(virtio_queue_t* queue) {
    if (queue->avail == NULL)
        return 0;
    return queue->avail->idx - queue->index;
}

static bool validate_queue_range(virtio_device_t* device, mx_vaddr_t addr, size_t size) {
    uintptr_t mem_addr = device->guest_physmem_addr;
    size_t mem_size = device->guest_physmem_size;
    mx_vaddr_t range_end = addr + size;
    mx_vaddr_t mem_end = mem_addr + mem_size;

    return addr >= mem_addr && range_end <= mem_end;
}

template <typename T>
static void queue_set_segment_addr(virtio_queue_t* queue, uint64_t guest_paddr, size_t size,
                                   T** ptr) {
    virtio_device_t* device = queue->virtio_device;
    mx_vaddr_t host_vaddr = guest_paddr_to_host_vaddr(device, guest_paddr);

    *ptr = validate_queue_range(device, host_vaddr, size)
               ? reinterpret_cast<T*>(host_vaddr)
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
    if (ring_avail_count(queue) > 0)
        cnd_signal(&queue->avail_ring_cnd);
    mtx_unlock(&queue->mutex);
}

mx_status_t virtio_device_notify(virtio_device_t* device) {
    return pci_interrupt(&device->pci_device);
}

mx_status_t virtio_device_kick(virtio_device_t* device, uint16_t queue_sel) {
    if (queue_sel >= device->num_queues)
        return MX_ERR_OUT_OF_RANGE;

    // Invoke the device callback if one has been provided.
    if (device->ops->queue_notify != NULL) {
        mx_status_t status = device->ops->queue_notify(device, queue_sel);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to handle queue notify event.\n");
            return status;
        }

        // Send an interrupt back to the guest if we've generated one while
        // processing the queue.
        fbl::AutoLock lock(&device->mutex);
        if (device->isr_status > 0) {
            return pci_interrupt(&device->pci_device);
        }
    }

    // Notify threads waiting on a descriptor.
    virtio_queue_signal(&device->queues[queue_sel]);
    return MX_OK;
}


// This must not return any errors besides MX_ERR_NOT_FOUND.
static mx_status_t virtio_queue_next_avail_locked(virtio_queue_t* queue, uint16_t* index) {
    if (ring_avail_count(queue) < 1)
        return MX_ERR_NOT_FOUND;

    *index = queue->avail->ring[ring_index(queue, queue->index++)];
    return MX_OK;
}

mx_status_t virtio_queue_next_avail(virtio_queue_t* queue, uint16_t* index) {
    mtx_lock(&queue->mutex);
    mx_status_t status = virtio_queue_next_avail_locked(queue, index);
    mtx_unlock(&queue->mutex);
    return status;
}

void virtio_queue_wait(virtio_queue_t* queue, uint16_t* index) {
    mtx_lock(&queue->mutex);
    while (virtio_queue_next_avail_locked(queue, index) == MX_ERR_NOT_FOUND)
        cnd_wait(&queue->avail_ring_cnd, &queue->mutex);
    mtx_unlock(&queue->mutex);
}

typedef struct poll_task_args {
    virtio_queue_t* queue;
    virtio_queue_poll_fn_t handler;
    void* ctx;
} poll_task_args_t;

static int virtio_queue_poll_task(void* ctx) {
    mx_status_t result = MX_OK;
    fbl::unique_ptr<poll_task_args_t> args(static_cast<poll_task_args_t*>(ctx));
    while (true) {
        uint16_t descriptor;
        virtio_queue_wait(args->queue, &descriptor);

        uint32_t used = 0;
        mx_status_t status = args->handler(args->queue, descriptor, &used, args->ctx);
        virtio_queue_return(args->queue, descriptor, used);

        if (status == MX_ERR_STOP)
            break;
        if (status != MX_OK) {
            fprintf(stderr, "Error %d while handling queue buffer.\n", status);
            result = status;
            break;
        }

        result = virtio_device_notify(args->queue->virtio_device);
        if (result != MX_OK)
            break;
    }

    return result;
}

mx_status_t virtio_queue_poll(virtio_queue_t* queue, virtio_queue_poll_fn_t handler, void* ctx) {
    poll_task_args_t* args = new poll_task_args_t{queue, handler, ctx};

    thrd_t thread;
    int ret = thrd_create(&thread, virtio_queue_poll_task, args);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create queue thread %d\n", ret);
        delete args;
        return MX_ERR_INTERNAL;
    }

    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach queue thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

mx_status_t virtio_queue_read_desc(virtio_queue_t* queue, uint16_t desc_index,
                                   virtio_desc_t* out) {
    virtio_device_t* device = queue->virtio_device;
    volatile struct vring_desc& desc = queue->desc[desc_index];
    size_t mem_size = device->guest_physmem_size;

    const uint64_t end = desc.addr + desc.len;
    if (end < desc.addr || end > mem_size)
        return MX_ERR_OUT_OF_RANGE;

    out->addr = reinterpret_cast<void*>(guest_paddr_to_host_vaddr(device, desc.addr));
    out->len = desc.len;
    out->has_next = desc.flags & VRING_DESC_F_NEXT;
    out->writable = desc.flags & VRING_DESC_F_WRITE;
    out->next = desc.next;
    return MX_OK;
}

void virtio_queue_return(virtio_queue_t* queue, uint16_t index, uint32_t len) {
    mtx_lock(&queue->mutex);

    volatile struct vring_used_elem* used =
        &queue->used->ring[ring_index(queue, queue->used->idx)];

    used->id = index;
    used->len = len;
    queue->used->idx++;

    mtx_unlock(&queue->mutex);

    // Set the queue bit in the device ISR so that the driver knows to check
    // the queues on the next interrupt.
    virtio_device_t* device = queue->virtio_device;
    mtx_lock(&device->mutex);
    device->isr_status |= VIRTIO_ISR_QUEUE;
    mtx_unlock(&device->mutex);
}

mx_status_t virtio_queue_handler(virtio_queue_t* queue, virtio_queue_fn_t handler, void* context) {
    uint16_t head;
    uint32_t used_len = 0;
    uintptr_t mem_addr = queue->virtio_device->guest_physmem_addr;
    size_t mem_size = queue->virtio_device->guest_physmem_size;

    // Get the next descriptor from the available ring. If none are available
    // we can just no-op.
    mx_status_t status = virtio_queue_next_avail(queue, &head);
    if (status == MX_ERR_NOT_FOUND)
        return MX_OK;
    if (status != MX_OK)
        return status;

    status = MX_OK;
    uint16_t desc_index = head;
    volatile const struct vring_desc* desc;
    do {
        desc = &queue->desc[desc_index];

        const uint64_t end = desc->addr + desc->len;
        if (end < desc->addr || end > mem_size)
            return MX_ERR_OUT_OF_RANGE;

        void* addr = reinterpret_cast<void*>(mem_addr + desc->addr);
        status = handler(addr, desc->len, desc->flags, &used_len, context);
        if (status != MX_OK) {
            fprintf(stderr, "Virtio request (%#lx, %u) failed %d\n", desc->addr, desc->len, status);
            return status;
        }

        desc_index = desc->next;
    } while (desc->flags & VRING_DESC_F_NEXT);

    virtio_queue_return(queue, head, used_len);

    return ring_avail_count(queue) > 0 ? MX_ERR_NEXT : MX_OK;
}

mx_status_t virtio_device_config_read(const virtio_device_t* device, void* config, uint16_t port,
                                      uint8_t access_size, mx_vcpu_io_t* vcpu_io) {
    vcpu_io->access_size = access_size;
    switch (access_size) {
    case 1: {
        uint8_t* buf = reinterpret_cast<uint8_t*>(config);
        vcpu_io->u8 = buf[port];
        return MX_OK;
    }
    case 2: {
        uint16_t* buf = reinterpret_cast<uint16_t*>(config);
        vcpu_io->u16 = buf[port/2];
        return MX_OK;
    }
    case 4: {
        uint32_t* buf = reinterpret_cast<uint32_t*>(config);
        vcpu_io->u32 = buf[port/4];
        return MX_OK;
    }
    }
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t virtio_device_config_write(const virtio_device_t* device, void* config, uint16_t port,
                                       const mx_vcpu_io_t* io) {
    switch (io->access_size) {
    case 1: {
        uint8_t* buf = reinterpret_cast<uint8_t*>(config);
        buf[port] = io->u8;
        return MX_OK;
    }
    case 2: {
        uint16_t* buf = reinterpret_cast<uint16_t*>(config);
        buf[port/2] = io->u16;
        return MX_OK;
    }
    case 4: {
        uint32_t* buf = reinterpret_cast<uint32_t*>(config);
        buf[port/4] = io->u32;
        return MX_OK;
    }
    }
    return MX_ERR_NOT_SUPPORTED;
}
