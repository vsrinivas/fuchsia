// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define VIRTIO_STATUS_ACKNOWLEDGE           (1u << 0)
#define VIRTIO_STATUS_DRIVER                (1u << 1)
#define VIRTIO_STATUS_DRIVER_OK             (1u << 2)
#define VIRTIO_STATUS_FEATURES_OK           (1u << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET    (1u << 6)
#define VIRTIO_STATUS_FAILED                (1u << 7)

// PCI IO space for transitional virtio devices
#define VIRTIO_PCI_DEVICE_FEATURES          0x0     // uint32_t
#define VIRTIO_PCI_DRIVER_FEATURES          0x4     // uint32_t
#define VIRTIO_PCI_QUEUE_PFN                0x8     // uint32_t
#define VIRTIO_PCI_QUEUE_SIZE               0xc     // uint16_t
#define VIRTIO_PCI_QUEUE_SELECT             0xe     // uint16_t
#define VIRTIO_PCI_QUEUE_NOTIFY             0x10    // uint16_t
#define VIRTIO_PCI_DEVICE_STATUS            0x12    // uint8_t
#define VIRTIO_PCI_ISR_STATUS               0x13    // uint8_t
#define VIRTIO_PCI_MSI_CONFIG_VECTOR        0x14    // uint16_t
#define VIRTIO_PCI_MSI_QUEUE_VECTOR         0x16    // uint16_t

#define VIRTIO_PCI_CONFIG_OFFSET_NOMSI      0x14    // uint16_t
#define VIRTIO_PCI_CONFIG_OFFSET_MSI        0x18    // uint16_t
