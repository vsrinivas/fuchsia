// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/bits.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <magenta/syscalls/port.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "virtio_priv.h"

/* Controls what PCI interface we expose. */
static const VirtioPciMode kVirtioPciMode = VirtioPciMode::LEGACY;

static uint8_t kPciCapTypeVendorSpecific = 0x9;

static uint16_t kPciVendorIdVirtio = 0x1af4;

/* Virtio PCI Bar Layout.
 *
 * We currently use a simple layout where all fields appear sequentially in
 * BAR 1 (this allows BAR 0 to be used for a transitional device if desired).
 * We place the device config as the last capability as it has a variable
 * length.
 *
 *        BAR0                   BAR1
 *    ------------  00h      ------------  00h
 *   | Virtio PCI |         | Virtio PCI |
 *   |   Legacy   |         |   Common   |
 *   |   Common   |         |   Config   |
 *   |   Config   |         |------------| 38h
 *   |------------| 14h     |   Notify   |
 *   |  Device-   |         |   Config   |
 *   | Specific   |         |------------| 3ah
 *   |  Config    |         | ISR Config |
 *   |            |         |------------| 3ch
 *   |            |         |  Device-   |
 *   |            |         | Specific   |
 *   |            |         |  Config    |
 *    ------------           ------------
 * These structures are defined in Virtio 1.0 Section 4.1.4.
 */
enum VirtioPciBar : uint8_t {
    LEGACY = 0,
    MODERN = 1,
};

// Common configuration.
static const size_t kVirtioPciCommonCfgBase = 0;
static const size_t kVirtioPciCommonCfgSize = 0x38;
static const size_t kVirtioPciCommonCfgTop = kVirtioPciCommonCfgBase + kVirtioPciCommonCfgSize - 1;
static_assert(kVirtioPciCommonCfgSize == sizeof(virtio_pci_common_cfg_t),
              "virtio_pci_common_cfg_t has unexpected size");
// Virtio 1.0 Section 4.1.4.3.1: offset MUST be 4-byte aligned.
static_assert(is_aligned(kVirtioPciCommonCfgBase, 4),
              "Virtio PCI common config has illegal alignment.");

// Notification configuration.
//
// Virtio 1.0 Section 4.1.4.4: notify_off_multiplier is combined with the
// queue_notify_off to derive the Queue Notify address within a BAR for a
// virtqueue:
//
//      cap.offset + queue_notify_off * notify_off_multiplier
//
// By fixing the multipler to 0 we provide only a single notify register for
// each device.
static const size_t kVirtioPciNotifyCfgMultiplier = 0;
static const size_t kVirtioPciNotifyCfgBase = 0x38;
static const size_t kVirtioPciNotifyCfgSize = 2;
static const size_t kVirtioPciNotifyCfgTop = kVirtioPciNotifyCfgBase + kVirtioPciNotifyCfgSize - 1;
// Virtio 1.0 Section 4.1.4.4.1: offset MUST be 2-byte aligned.
static_assert(is_aligned(kVirtioPciNotifyCfgBase, 2),
              "Virtio PCI notify config has illegal alignment.");

// Interrupt status configuration.
static const size_t kVirtioPciIsrCfgBase = 0x3a;
static const size_t kVirtioPciIsrCfgSize = 1;
static const size_t kVirtioPciIsrCfgTop = kVirtioPciIsrCfgBase + kVirtioPciIsrCfgSize - 1;
// Virtio 1.0 Section 4.1.4.5: The offset for the ISR status has no alignment
// requirements.

// Device-specific configuration.
static const size_t kVirtioPciDeviceCfgBase = 0x3c;
// Virtio 1.0 Section 4.1.4.6.1: The offset for the device-specific
// configuration MUST be 4-byte aligned.
static_assert(is_aligned(kVirtioPciDeviceCfgBase, 4),
              "Virtio PCI notify config has illegal alignment.");

static virtio_device_t* pci_device_to_virtio(const pci_device_t* device) {
    return static_cast<virtio_device_t*>(device->impl);
}

static virtio_queue_t* selected_queue(const virtio_device_t* device) {
    return device->queue_sel < device->num_queues ? &device->queues[device->queue_sel] : nullptr;
}

/* Handle reads to the common configuration structure as defined in
 * Virtio 1.0 Section 4.1.4.3.
 */
static mx_status_t virtio_pci_common_cfg_read(const pci_device_t* pci_device, uint16_t port,
                                              uint8_t access_size, mx_vcpu_io_t* vcpu_io) {
    virtio_device_t* device = pci_device_to_virtio(pci_device);
    switch (port) {
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL: {
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u32 = device->driver_features_sel;
        vcpu_io->access_size = 4;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL: {
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u32 = device->features_sel;
        vcpu_io->access_size = 4;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES: {
        // We currently only support a single feature word.
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u32 = device->driver_features_sel > 0 ? 0 : device->driver_features;
        vcpu_io->access_size = 4;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES: {
        // Virtio 1.0 Section 6:
        //
        // A device MUST offer VIRTIO_F_VERSION_1.
        //
        // VIRTIO_F_VERSION_1(32) This indicates compliance with this
        // specification, giving a simple way to detect legacy devices or
        // drivers.
        //
        // This is the only feature supported beyond the first feature word so
        // we just specaial case it here.
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->access_size = 4;
        if (device->features_sel == 1) {
            vcpu_io->u32 = 1;
            return MX_OK;
        }

        vcpu_io->u32 = device->features_sel > 0 ? 0 : device->features;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_NUM_QUEUES: {
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u16 = device->num_queues;
        vcpu_io->access_size = 2;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS: {
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u8 = device->status;
        vcpu_io->access_size = 1;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SEL: {
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u16 = device->queue_sel;
        vcpu_io->access_size = 2;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE: {
        virtio_queue_t* queue = selected_queue(device);
        if (queue == nullptr)
            return MX_ERR_BAD_STATE;

        fbl::AutoLock lock(&queue->mutex);
        vcpu_io->u16 = queue->size;
        vcpu_io->access_size = 2;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE:
        // Virtio 1.0 Section 4.1.4.3: The device MUST present a 0 in
        // queue_enable on reset.
        //
        // Note the implementation currently does not respect this value.
        vcpu_io->access_size = 2;
        vcpu_io->u16 = 0;
        return MX_OK;
    case VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW ... VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH: {
        virtio_queue_t* queue = selected_queue(device);
        if (queue == nullptr)
            return MX_ERR_BAD_STATE;

        size_t word = (port - VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW) / sizeof(uint32_t);
        fbl::AutoLock lock(&queue->mutex);
        vcpu_io->u32 = queue->addr.words[word];
        vcpu_io->access_size = 4;
        return MX_OK;
    }

    // Currently not implmeneted.
    case VIRTIO_PCI_COMMON_CFG_CONFIG_GEN:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF:
    case VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG:
        vcpu_io->u32 = 0;
        vcpu_io->access_size = access_size;
        return MX_OK;
    }
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t virtio_pci_read(const pci_device_t* pci_device, uint8_t bar, uint16_t port,
                                   uint8_t access_size, mx_vcpu_io_t* vcpu_io) {
    if (bar != VirtioPciBar::MODERN)
        return MX_ERR_NOT_SUPPORTED;

    virtio_device_t* device = pci_device_to_virtio(pci_device);
    switch (port) {
    case kVirtioPciCommonCfgBase ... kVirtioPciCommonCfgTop:
        return virtio_pci_common_cfg_read(pci_device, port - kVirtioPciCommonCfgBase,
                                          access_size, vcpu_io);
    case kVirtioPciIsrCfgBase ... kVirtioPciIsrCfgTop:
        fbl::AutoLock lock(&device->mutex);
        vcpu_io->u8 = device->isr_status;
        vcpu_io->access_size = 1;

        // From VIRTIO 1.0 Section 4.1.4.5:
        //
        // To avoid an extra access, simply reading this register resets it to
        // 0 and causes the device to de-assert the interrupt.
        device->isr_status = 0;
        return MX_OK;
    }

    if (port >= kVirtioPciDeviceCfgBase && port < kVirtioPciDeviceCfgBase + device->config_size) {
        uint16_t device_offset = static_cast<uint16_t>(port - kVirtioPciDeviceCfgBase);
        return device->ops->read(device, device_offset, access_size, vcpu_io);
    }
    fprintf(stderr, "Unhandled read %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

static void virtio_queue_update_addr(virtio_queue_t* queue) {
    virtio_queue_set_desc_addr(queue, queue->addr.desc);
    virtio_queue_set_avail_addr(queue, queue->addr.avail);
    virtio_queue_set_used_addr(queue, queue->addr.used);
}

/* Handle writes to the common configuration structure as defined in
 * Virtio 1.0 Section 4.1.4.3.
 */
static mx_status_t virtio_pci_common_cfg_write(pci_device_t* pci_device, uint16_t port,
                                               const mx_vcpu_io_t* io) {
    virtio_device_t* device = pci_device_to_virtio(pci_device);

    switch (port) {
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL: {
        if (io->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device->mutex);
        device->features_sel = io->u32;
        return MX_OK;
    }

    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL: {
        if (io->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device->mutex);
        device->driver_features_sel = io->u32;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES: {
        if (io->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device->mutex);
        if (device->driver_features_sel == 0)
            device->driver_features = io->u32;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS: {
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device->mutex);
        device->status = io->u8;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SEL: {
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (io->u16 >= device->num_queues)
            return MX_ERR_NOT_SUPPORTED;

        fbl::AutoLock lock(&device->mutex);
        device->queue_sel = io->u16;
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE: {
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        virtio_queue_t* queue = selected_queue(device);
        if (queue == nullptr)
            return MX_ERR_BAD_STATE;

        fbl::AutoLock lock(&queue->mutex);
        queue->size = io->u16;
        virtio_queue_update_addr(queue);
        return MX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW ... VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH: {
        if (io->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;
        virtio_queue_t* queue = selected_queue(device);
        if (queue == nullptr)
            return MX_ERR_BAD_STATE;

        size_t word = (port - VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW) / sizeof(uint32_t);
        fbl::AutoLock lock(&queue->mutex);
        queue->addr.words[word] = io->u32;
        virtio_queue_update_addr(queue);
        return MX_OK;
    }
    // Not implemented registers.
    case VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR:
    case VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE:
        return MX_OK;
    // Read-only registers.
    case VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF:
    case VIRTIO_PCI_COMMON_CFG_NUM_QUEUES:
    case VIRTIO_PCI_COMMON_CFG_CONFIG_GEN:
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES:
        fprintf(stderr, "Unsupported write to %x\n", port);
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t virtio_pci_write(pci_device_t* pci_device, uint8_t bar, uint16_t port,
                                    const mx_vcpu_io_t* io) {
    if (bar != VirtioPciBar::MODERN)
        return MX_ERR_NOT_SUPPORTED;

    virtio_device_t* device = pci_device_to_virtio(pci_device);
    switch (port) {
    case kVirtioPciCommonCfgBase ... kVirtioPciCommonCfgTop: {
        uint16_t offset = port - kVirtioPciCommonCfgBase;
        return virtio_pci_common_cfg_write(pci_device, offset, io);
    }
    case kVirtioPciNotifyCfgBase ... kVirtioPciNotifyCfgTop:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;

        return virtio_device_kick(device, io->u16);
    }

    if (port >= kVirtioPciDeviceCfgBase && port < kVirtioPciDeviceCfgBase + device->config_size) {
        uint16_t device_offset = static_cast<uint16_t>(port - kVirtioPciDeviceCfgBase);
        return device->ops->write(device, device_offset, io);
    }
    fprintf(stderr, "Unhandled write %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t virtio_pci_transitional_write(pci_device_t* pci_device, uint8_t bar,
                                                 uint16_t port, const mx_vcpu_io_t* io) {
    if (bar == VirtioPciBar::LEGACY)
        return virtio_pci_legacy_write(pci_device, bar, port, io);

    return virtio_pci_write(pci_device, bar, port, io);
}

static mx_status_t virtio_pci_transitional_read(const pci_device_t* pci_device, uint8_t bar,
                                                uint16_t port, uint8_t access_size,
                                                mx_vcpu_io_t* vcpu_io) {
    if (bar == VirtioPciBar::LEGACY)
        return virtio_pci_legacy_read(pci_device, bar, port, access_size, vcpu_io);

    return virtio_pci_read(pci_device, bar, port, access_size, vcpu_io);
}

/* Transitional ops expose the legacy interface on BAR 0 and the modern
 * interface on BAR 1.
 */
static const pci_device_ops_t kVirtioPciTransitionalDeviceOps = {
    .read_bar = &virtio_pci_transitional_read,
    .write_bar = &virtio_pci_transitional_write,
};

/* Legacy ops expose the legacy interface on BAR 0 only. */
static const pci_device_ops_t kVirtioPciLegacyDeviceOps = {
    .read_bar = &virtio_pci_legacy_read,
    .write_bar = &virtio_pci_legacy_write,
};

/* Expose the modern interface on BAR 1 only. */
static const pci_device_ops_t kVirtioPciDeviceOps = {
    .read_bar = &virtio_pci_read,
    .write_bar = &virtio_pci_write,
};

static void virtio_pci_setup_cap(pci_cap_t* cap, virtio_pci_cap_t* virtio_cap, uint8_t cfg_type,
                                 size_t cap_len, size_t data_length, size_t bar_offset) {
    virtio_cap->cfg_type = cfg_type;
    virtio_cap->cap_len = static_cast<uint8_t>(cap_len);
    virtio_cap->bar = VirtioPciBar::MODERN;
    virtio_cap->offset = static_cast<uint32_t>(bar_offset);
    virtio_cap->length = static_cast<uint32_t>(data_length);

    cap->id = kPciCapTypeVendorSpecific;
    cap->data = reinterpret_cast<uint8_t*>(virtio_cap);
    cap->len = sizeof(*virtio_cap);
}

static void virtio_pci_setup_caps(virtio_device_t* device) {
    // Common configuration.
    virtio_pci_setup_cap(&device->capabilities[0], &device->common_cfg_cap,
                         VIRTIO_PCI_CAP_COMMON_CFG, sizeof(device->common_cfg_cap),
                         kVirtioPciCommonCfgSize, kVirtioPciCommonCfgBase);

    // Notify configuration.
    device->notify_cfg_cap.notify_off_multiplier = kVirtioPciNotifyCfgMultiplier;
    virtio_pci_setup_cap(&device->capabilities[1], &device->notify_cfg_cap.cap,
                         VIRTIO_PCI_CAP_NOTIFY_CFG, sizeof(device->notify_cfg_cap),
                         kVirtioPciNotifyCfgSize, kVirtioPciNotifyCfgBase);

    // ISR configuration.
    virtio_pci_setup_cap(&device->capabilities[2], &device->isr_cfg_cap,
                         VIRTIO_PCI_CAP_ISR_CFG, sizeof(device->isr_cfg_cap),
                         kVirtioPciIsrCfgSize, kVirtioPciIsrCfgBase);

    // Device-specific configuration.
    virtio_pci_setup_cap(&device->capabilities[3], &device->device_cfg_cap,
                         VIRTIO_PCI_CAP_DEVICE_CFG, sizeof(device->device_cfg_cap),
                         device->config_size, kVirtioPciDeviceCfgBase);

    // Note VIRTIO_PCI_CAP_PCI_CFG is not implmeneted.
    // This one is more complex since it is writable and doesn't seem to be
    // used by Linux or Magenta.

    static_assert(kVirtioPciNumCapabilities == 4, "Incorrect number of capabilities.");
    device->pci_device.capabilities = device->capabilities;
    device->pci_device.num_capabilities = kVirtioPciNumCapabilities;

    static_assert(VirtioPciBar::MODERN < PCI_MAX_BARS, "Not enough BAR registers available.");
    device->pci_device.bar[VirtioPciBar::MODERN].size = static_cast<uint32_t>(
          kVirtioPciDeviceCfgBase + device->config_size);
}

static void virtio_pci_setup_legacy_bar(virtio_device_t* device) {
    uint16_t legacy_config_size = static_cast<uint16_t>(
        sizeof(virtio_pci_legacy_config_t) + device->config_size);
    device->pci_device.bar[0].size = legacy_config_size;
}

static constexpr uint16_t virtio_pci_id(VirtioPciMode mode, uint16_t virtio_id) {
    if (mode == VirtioPciMode::MODERN) {
        return static_cast<uint16_t>(virtio_id + 0x1040u);
    }
    // Virtio 1.0 Section 4.1.2.3: Transitional devices MUST have the
    // Transitional PCI Device ID in the range 0x1000 to 0x103f.
    return static_cast<uint16_t>(virtio_id + 0xfffu);
}

void virtio_pci_init(virtio_device_t* device) {
    device->pci_device.vendor_id = kPciVendorIdVirtio;
    device->pci_device.device_id = virtio_pci_id(kVirtioPciMode, device->device_id);
    device->pci_device.subsystem_vendor_id = 0;
    device->pci_device.subsystem_id = device->device_id;
    device->pci_device.class_code = 0;
    device->pci_device.impl = device;

    switch (kVirtioPciMode) {
    case VirtioPciMode::LEGACY:
        device->pci_device.revision_id = 0;
        device->pci_device.ops = &kVirtioPciLegacyDeviceOps;
        virtio_pci_setup_legacy_bar(device);
        break;
    case VirtioPciMode::MODERN:
        // Virtio 1.0 Section 4.1.2.1: Non-transitional devices SHOULD have a
        // PCI Revision ID of 1 or higher.
        device->pci_device.revision_id = 1;
        device->pci_device.ops = &kVirtioPciDeviceOps;
        virtio_pci_setup_caps(device);
        break;
    case VirtioPciMode::TRANSITIONAL:
        // Virtio 1.0 Section 4.1.2.3: Transitional devices MUST have a PCI
        // Revision ID of 0.
        device->pci_device.revision_id = 0;
        device->pci_device.ops = &kVirtioPciTransitionalDeviceOps;
        virtio_pci_setup_caps(device);
        virtio_pci_setup_legacy_bar(device);
        break;
    }
}
