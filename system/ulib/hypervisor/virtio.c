// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <hypervisor/virtio.h>
#include <virtio/virtio_ring.h>

// Returns a circular index into a Virtio ring.
static uint32_t ring_index(virtio_queue_t* queue, uint32_t index) {
    return index % queue->size;
}

mx_status_t handle_virtio_queue(virtio_queue_t* queue, void* mem_addr, size_t mem_size,
                                uint32_t hdr_size, virtio_req_fn_t req_fn, void* ctx) {
    for (; queue->index < queue->avail->idx; queue->index++, queue->used->idx++) {
        uint16_t desc_index = queue->avail->ring[ring_index(queue, queue->index)];
        if (desc_index >= queue->size)
            return MX_ERR_OUT_OF_RANGE;
        volatile struct vring_used_elem* used =
            &queue->used->ring[ring_index(queue, queue->used->idx)];
        used->id = desc_index;

        void* req = NULL;
        bool has_payload = false;
        uint8_t req_status = VIRTIO_STATUS_OK;
        while (true) {
            struct vring_desc desc = queue->desc[desc_index];
            const uint64_t end = desc.addr + desc.len;
            if (end < desc.addr || end > mem_size)
                return MX_ERR_OUT_OF_RANGE;
            if (req == NULL) {
                // Header.
                if (desc.len != hdr_size)
                    return MX_ERR_INVALID_ARGS;
                req = mem_addr + desc.addr;
            } else if (desc.flags & VRING_DESC_F_NEXT) {
                // Payload.
                has_payload = true;
                mx_status_t status = req_fn(ctx, req, mem_addr + desc.addr, desc.len);
                if (status != MX_OK) {
                    fprintf(stderr, "Virtio request (%#lx, %u) failed %d\n",
                            desc.addr, desc.len, status);
                    req_status = VIRTIO_STATUS_ERROR;
                } else {
                    used->len += desc.len;
                }
            } else {
                // Status.
                if (desc.len != sizeof(uint8_t))
                    return MX_ERR_INVALID_ARGS;
                // If there was no payload, call the request function once.
                if (!has_payload) {
                    mx_status_t status = req_fn(ctx, req, NULL, 0);
                    if (status != MX_OK)
                        req_status = VIRTIO_STATUS_ERROR;
                }
                uint8_t* virtio_status = mem_addr + desc.addr;
                *virtio_status = req_status;
                break;
            }
            desc_index = desc.next;
        };
    }
    return MX_OK;
}
