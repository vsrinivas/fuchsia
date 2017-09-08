// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/pci.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>
#include <virtio/virtio.h>

/* Virtio PCI interface types. */
enum class VirtioPciMode {
    // As defined in Virtio 0.9.5.
    LEGACY,
    // As defined as a 'non-transitional device' in Virtio 1.0 spec.
    MODERN,
    // As defined as a 'transitional device' in Virtio 1.0 spec.
    TRANSITIONAL,
};


/* Legacy PCI device functions. */
mx_status_t virtio_pci_legacy_write(pci_device_t* pci_device, uint8_t bar, uint16_t port,
                                    const mx_vcpu_io_t* io);

mx_status_t virtio_pci_legacy_read(const pci_device_t* pci_device, uint8_t bar, uint16_t port,
                                   uint8_t access_size, mx_vcpu_io_t* vcpu_io);

/* Read bytes from a devices config structure.
 *
 * |config| must point to an in-memory representation of the config structure
 * that will be addressed by software.
 */
mx_status_t virtio_device_config_read(const virtio_device_t* device, void* config, uint16_t port,
                                      uint8_t access_size, mx_vcpu_io_t* vcpu_io);

/* Write bytes to a devices config structure.
 *
 * |config| must point to an in-memory representation of the config structure
 * that will be addressed by software.
 */
mx_status_t virtio_device_config_write(const virtio_device_t* device, void* config, uint16_t port,
                                       const mx_vcpu_io_t* io);
