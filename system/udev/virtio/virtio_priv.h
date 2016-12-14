// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

// clang-format off

struct virtio_mmio_config {
    /* 0x00 */
    uint32_t magic;
    uint32_t version;
    uint32_t device_id;
    uint32_t vendor_id;
    /* 0x10 */
    uint32_t device_features;
    uint32_t device_features_sel;
    uint32_t __reserved0[2];
    /* 0x20 */
    uint32_t driver_features;
    uint32_t driver_features_sel;
    uint32_t guest_page_size;
    uint32_t __reserved1[1];
    /* 0x30 */
    uint32_t queue_sel;
    uint32_t queue_num_max;
    uint32_t queue_num;
    uint32_t queue_align;
    /* 0x40 */
    uint32_t queue_pfn;
    uint32_t __reserved2[3];
    /* 0x50 */
    uint32_t queue_notify;
    uint32_t __reserved3[3];
    /* 0x60 */
    uint32_t interrupt_status;
    uint32_t interrupt_ack;
    uint32_t __reserved4[2];
    /* 0x70 */
    uint32_t status;
    uint8_t __reserved5[0x8c];
    /* 0x100 */
    uint32_t config[0];
};

static_assert(sizeof(struct virtio_mmio_config) == 0x100);

#define VIRTIO_MMIO_MAGIC 0x74726976 // 'virt'

#define VIRTIO_STATUS_ACKNOWLEDGE       (1<<0)
#define VIRTIO_STATUS_DRIVER            (1<<1)
#define VIRTIO_STATUS_DRIVER_OK         (1<<2)
#define VIRTIO_STATUS_FEATURES_OK       (1<<3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET (1<<6)
#define VIRTIO_STATUS_FAILED            (1<<7)

// PCI IO space for transitional virtio devices
#define VIRTIO_PCI_DEVICE_FEATURES      (0x0) // 32
#define VIRTIO_PCI_DRIVER_FEATURES      (0x4) // 32
#define VIRTIO_PCI_QUEUE_PFN            (0x8) // 32
#define VIRTIO_PCI_QUEUE_SIZE           (0xc) // 16
#define VIRTIO_PCI_QUEUE_SELECT         (0xe) // 16
#define VIRTIO_PCI_QUEUE_NOTIFY         (0x10) // 16
#define VIRTIO_PCI_DEVICE_STATUS        (0x12) // 8
#define VIRTIO_PCI_ISR_STATUS           (0x13) // 8
#define VIRTIO_PCI_MSI_CONFIG_VECTOR    (0x14) // 16
#define VIRTIO_PCI_MSI_QUEUE_VECTOR     (0x16) // 16

#define VIRTIO_PCI_CONFIG_OFFSET_NOMSI  (0x14) // 16
#define VIRTIO_PCI_CONFIG_OFFSET_MSI    (0x18) // 16

// non transitional common configuration
struct virtio_pci_common_cfg {
    // device info
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;

    // about specific queue
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
};


