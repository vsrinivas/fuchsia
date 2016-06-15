// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <compiler.h>
#include <assert.h>
#include <list.h>
#include <sys/types.h>
#include <dev/virtio/virtio_ring.h>

/* Device IDs taken from Section 5 of the VirtIO 1.0 Draft Specification
 * http://docs.oasis-open.org/virtio/virtio/v1.0/csprd01/virtio-v1.0-csprd01.pdf */
#define VIRTIO_DEV_ID_INVALID       0x00
#define VIRTIO_DEV_ID_NET           0x01
#define VIRTIO_DEV_ID_BLOCK         0x02
#define VIRTIO_DEV_ID_CONSOLE       0x03
#define VIRTIO_DEV_ID_ENTROPY_SRC   0x04
#define VIRTIO_DEV_ID_MEM_BALLOON   0x05
#define VIRTIO_DEV_ID_IO_MEMORY     0x06
#define VIRTIO_DEV_ID_RPMSG         0x07
#define VIRTIO_DEV_ID_SCSI_HOST     0x08
#define VIRTIO_DEV_ID_9P_TRANSPORT  0x09
#define VIRTIO_DEV_ID_MAC80211_WLAN 0x0A
#define VIRTIO_DEV_ID_RPROC_SERIAL  0x0B
#define VIRTIO_DEV_ID_CAIF          0x0C
#define VIRTIO_DEV_ID_GPU           0x10
#define VIRTIO_DEV_ID_INPUT         0x12

/* detect a virtio mmio hardware block
 * returns number of devices found */
int virtio_mmio_detect(void *ptr, uint count, const uint irqs[]);

#define MAX_VIRTIO_RINGS 4

struct virtio_mmio_config;

struct virtio_device {
    bool valid;

    uint index;
    uint irq;

    volatile struct virtio_mmio_config *mmio_config;
    void *config_ptr;

    void *priv; /* a place for the driver to put private data */

    enum handler_return (*irq_driver_callback)(struct virtio_device *dev, uint ring, const struct vring_used_elem *e);
    enum handler_return (*config_change_callback)(struct virtio_device *dev);

    /* virtio rings */
    uint32_t active_rings_bitmap;
    struct vring ring[MAX_VIRTIO_RINGS];
};

void virtio_reset_device(struct virtio_device *dev);
void virtio_status_acknowledge_driver(struct virtio_device *dev);
void virtio_status_driver_ok(struct virtio_device *dev);

/* api used by devices to interact with the virtio bus */
status_t virtio_alloc_ring(struct virtio_device *dev, uint index, uint16_t len) __NONNULL();

/* add a descriptor at index desc_index to the free list on ring_index */
void virtio_free_desc(struct virtio_device *dev, uint ring_index, uint16_t desc_index);

/* add the descriptor(s) in the chain starting at chain_head to the free list on ring_index */
void virtio_free_desc_chain(struct virtio_device *dev, uint ring_index, uint16_t chain_head);

/* allocate a descriptor off the free list, 0xffff is error */
uint16_t virtio_alloc_desc(struct virtio_device *dev, uint ring_index);

/* allocate a descriptor chain the free list */
struct vring_desc *virtio_alloc_desc_chain(struct virtio_device *dev, uint ring_index, size_t count, uint16_t *start_index);

static inline struct vring_desc *virtio_desc_index_to_desc(struct virtio_device *dev, uint ring_index, uint16_t desc_index)
{
    DEBUG_ASSERT(desc_index != 0xffff);
    return &dev->ring[ring_index].desc[desc_index];
}

void virtio_dump_desc(const struct vring_desc *desc);

/* submit a chain to the avail list */
void virtio_submit_chain(struct virtio_device *dev, uint ring_index, uint16_t desc_index);

void virtio_kick(struct virtio_device *dev, uint ring_idnex);

/* class driver registration */
typedef void(*virtio_module_init_func)(void);
typedef status_t(*virtio_init_func)(struct virtio_device*, uint32_t);
typedef status_t(*virtio_starup_func)(struct virtio_device*);
typedef struct virtio_dev_class {
    uint32_t                device_id;
    const char*             name;
    virtio_module_init_func module_init_fn;
    virtio_init_func        init_fn;
    virtio_starup_func      startup_fn;
} virtio_dev_class_t;

#define VIRTIO_DEV_CLASS(_name, id, mod_init, init, startup) \
extern const virtio_dev_class_t __virtio_class_##_name; \
const virtio_dev_class_t __virtio_class_##_name __SECTION("virtio_classes") = { \
    .device_id = id, \
    .name = #_name, \
    .module_init_fn = mod_init, \
    .init_fn = init, \
    .startup_fn = startup, \
};

