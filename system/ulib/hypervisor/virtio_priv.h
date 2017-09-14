// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/pci.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>
#include <virtio/virtio.h>

/* Read bytes from a devices config structure.
 *
 * |config| must point to an in-memory representation of the config structure
 * that will be addressed by software.
 */
zx_status_t virtio_device_config_read(const virtio_device_t* device, void* config, uint16_t port,
                                      uint8_t access_size, zx_vcpu_io_t* vcpu_io);

/* Write bytes to a devices config structure.
 *
 * |config| must point to an in-memory representation of the config structure
 * that will be addressed by software.
 */
zx_status_t virtio_device_config_write(const virtio_device_t* device, void* config, uint16_t port,
                                       const zx_vcpu_io_t* io);
