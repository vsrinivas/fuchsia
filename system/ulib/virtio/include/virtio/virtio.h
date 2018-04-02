// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

#define VIRTIO_STATUS_ACKNOWLEDGE                   (1u << 0)
#define VIRTIO_STATUS_DRIVER                        (1u << 1)
#define VIRTIO_STATUS_DRIVER_OK                     (1u << 2)
#define VIRTIO_STATUS_FEATURES_OK                   (1u << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET            (1u << 6)
#define VIRTIO_STATUS_FAILED                        (1u << 7)

// PCI config space for non-transitional devices.
#define VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL   0x0
#define VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES       0x4
#define VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL   0x8
#define VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES       0xc
#define VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG           0x10
#define VIRTIO_PCI_COMMON_CFG_NUM_QUEUES            0x12
#define VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS         0x14
#define VIRTIO_PCI_COMMON_CFG_CONFIG_GEN            0x15
#define VIRTIO_PCI_COMMON_CFG_QUEUE_SEL             0x16
#define VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE            0x18
#define VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR     0x1a
#define VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE          0x1c
#define VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF      0x1e
#define VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW        0x20
#define VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_HIGH       0x24
#define VIRTIO_PCI_COMMON_CFG_QUEUE_AVAIL_LOW       0x28
#define VIRTIO_PCI_COMMON_CFG_QUEUE_AVAIL_HIGH      0x2c
#define VIRTIO_PCI_COMMON_CFG_QUEUE_USED_LOW        0x30
#define VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH       0x34

// PCI IO space for transitional virtio devices
#define VIRTIO_PCI_DEVICE_FEATURES                  0x0     // uint32_t
#define VIRTIO_PCI_DRIVER_FEATURES                  0x4     // uint32_t
#define VIRTIO_PCI_QUEUE_PFN                        0x8     // uint32_t
#define VIRTIO_PCI_QUEUE_SIZE                       0xc     // uint16_t
#define VIRTIO_PCI_QUEUE_SELECT                     0xe     // uint16_t
#define VIRTIO_PCI_QUEUE_NOTIFY                     0x10    // uint16_t
#define VIRTIO_PCI_DEVICE_STATUS                    0x12    // uint8_t
#define VIRTIO_PCI_ISR_STATUS                       0x13    // uint8_t
#define VIRTIO_PCI_MSI_CONFIG_VECTOR                0x14    // uint16_t
#define VIRTIO_PCI_MSI_QUEUE_VECTOR                 0x16    // uint16_t

#define VIRTIO_PCI_CONFIG_OFFSET_NOMSIX             0x14    // uint16_t
#define VIRTIO_PCI_CONFIG_OFFSET_MSIX               0x18    // uint16_t

#define VIRTIO_PCI_CAP_COMMON_CFG                   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG                   2
#define VIRTIO_PCI_CAP_ISR_CFG                      3
#define VIRTIO_PCI_CAP_DEVICE_CFG                   4
#define VIRTIO_PCI_CAP_PCI_CFG                      5

#define VIRTIO_ISR_QUEUE_INT                        0x1
#define VIRTIO_ISR_DEV_CFG_INT                      0x2

#define VIRTIO_F_RING_INDIRECT_DESC                 28
#define VIRTIO_F_RING_EVENT_IDX                     29
#define VIRTIO_F_VERSION_1                          32

__BEGIN_CDECLS

typedef struct virtio_pci_cap {
    uint8_t cap_vndr;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
} __PACKED virtio_pci_cap_t;

typedef struct virtio_pci_notify_cap {
  virtio_pci_cap_t cap;
  uint32_t notify_off_multiplier;
} __PACKED virtio_pci_notify_cap_t;

typedef struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;

    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __PACKED virtio_pci_common_cfg_t;

#define VIRTIO_PCI_VENDOR_ID            0x1af4

// Device ids
#define VIRTIO_DEV_ID_RESERVED          0
#define VIRTIO_DEV_ID_NETWORK           1
#define VIRTIO_DEV_ID_BLOCK             2
#define VIRTIO_DEV_ID_CONSOLE           3
#define VIRTIO_DEV_ID_ENTROPY           4
#define VIRTIO_DEV_ID_BALLOON_T         5
#define VIRTIO_DEV_ID_IOMEMORY          6
#define VIRTIO_DEV_ID_RPMSG             7
#define VIRTIO_DEV_ID_SCSI_HOST         8
#define VIRTIO_DEV_ID_9P_TRANS          9
#define VIRTIO_DEV_ID_MAC80211          10
#define VIRTIO_DEV_ID_RPROC             11
#define VIRTIO_DEV_ID_CAIF              12
#define VIRTIO_DEV_ID_BALLOON           13
// Intentional gap of 14-15
#define VIRTIO_DEV_ID_GPU               16
#define VIRTIO_DEV_ID_TIMER             17
#define VIRTIO_DEV_ID_INPUT             18

// Device Types (transitional)
#define VIRTIO_DEV_TYPE_T_NETWORK       0x1000
#define VIRTIO_DEV_TYPE_T_BLOCK         0x1001
#define VIRTIO_DEV_TYPE_T_BALLOON       0x1002
#define VIRTIO_DEV_TYPE_T_CONSOLE       0x1003
#define VIRTIO_DEV_TYPE_T_SCSI_HOST     0x1004
#define VIRTIO_DEV_TYPE_T_ENTROPY       0x1005
#define VIRTIO_DEV_TYPE_T_9P            0x1009

// Legacy Device Types
#define VIRTIO_LEGACY_DEV_TYPE(dev_id) (0x1040 + dev_id)
#define VIRTIO_DEV_TYPE_NETWORK        VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_NETWORK)
#define VIRTIO_DEV_TYPE_BLOCK          VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_BLOCK)
#define VIRTIO_DEV_TYPE_BALLOON        VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_BALLOON)
#define VIRTIO_DEV_TYPE_CONSOLE        VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_CONSOLE)
#define VIRTIO_DEV_TYPE_SCSI           VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_SCSI_HOST)
#define VIRTIO_DEV_TYPE_ENTROPY        VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_ENTROPY)
#define VIRTIO_DEV_TYPE_9P             VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_9P)
#define VIRTIO_DEV_TYPE_GPU            VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_GPU)
#define VIRTIO_DEV_TYPE_INPUT          VIRTIO_LEGACY_DEV_TYPE(VIRTIO_DEV_ID_INPUT)

__END_CDECLS
