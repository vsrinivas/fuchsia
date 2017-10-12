// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/virtio_pci.h>

#include <stdio.h>

#include <fbl/auto_lock.h>
#include <hypervisor/bits.h>
#include <hypervisor/virtio.h>

static uint8_t kPciCapTypeVendorSpecific = 0x9;

static uint16_t kPciVendorIdVirtio = 0x1af4;

/* Virtio PCI Bar Layout.
 *
 * Expose all read/write fields on BAR0 using a strongly ordered mapping.
 * Map the Queue notify region to BAR1 with a BELL type that does not require
 * the guest to decode any instruction fields. The queue to notify can be
 * inferred based on the address accessed alone.
 *
 *          BAR0                BAR1
 *      ------------  00h   ------------  00h
 *     | Virtio PCI |      |  Queue 0   |
 *     |   Common   |      |   Notify   |
 *     |   Config   |      |------------| 04h
 *     |------------| 38h  |  Queue 1   |
 *     | ISR Config |      |   Notify   |
 *     |------------| 3ch  |------------|
 *     |  Device-   |      |    ...     |
 *     | Specific   |      |------------| 04 * N
 *     |  Config    |      |  Queue N   |
 *     |            |      |   Notify   |
 *      ------------        ------------
 * These structures are defined in Virtio 1.0 Section 4.1.4.
 */
static const uint8_t kVirtioPciBar = 0;
static const uint8_t kVirtioPciNotifyBar = 1;

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
// Virtio 1.0 Section 4.1.4.4.1: The device MUST either present
// notify_off_multiplier as an even power of 2, or present
// notify_off_multiplier as 0.
//
// By using a multiplier of 4, we use sequential 4b words to notify, ex:
//
//      cap.offset + 0  -> Notify Queue 0
//      cap.offset + 4  -> Notify Queue 1
//      ...
//      cap.offset + 4n -> Notify Queuen 'n'
static const size_t kVirtioPciNotifyCfgMultiplier = 4;
static const size_t kVirtioPciNotifyCfgBase = 0;
// Virtio 1.0 Section 4.1.4.4.1: offset MUST be 2-byte aligned.
static_assert(is_aligned(kVirtioPciNotifyCfgBase, 2),
              "Virtio PCI notify config has illegal alignment.");

// Interrupt status configuration.
static const size_t kVirtioPciIsrCfgBase = 0x38;
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

/* Handle reads to the common configuration structure as defined in
 * Virtio 1.0 Section 4.1.4.3.
 */
zx_status_t VirtioPci::CommonCfgRead(uint64_t addr, IoValue* value) {
    switch (addr) {
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL: {
        fbl::AutoLock lock(&device_->mutex_);
        value->u32 = device_->driver_features_sel_;
        value->access_size = 4;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL: {
        fbl::AutoLock lock(&device_->mutex_);
        value->u32 = device_->features_sel_;
        value->access_size = 4;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES: {
        // We currently only support a single feature word.
        fbl::AutoLock lock(&device_->mutex_);
        value->u32 = device_->driver_features_sel_ > 0 ? 0 : device_->driver_features_;
        value->access_size = 4;
        return ZX_OK;
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
        fbl::AutoLock lock(&device_->mutex_);
        value->access_size = 4;
        if (device_->features_sel_ == 1) {
            value->u32 = 1;
            return ZX_OK;
        }

        value->u32 = device_->features_sel_ > 0 ? 0 : device_->features_;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_NUM_QUEUES: {
        fbl::AutoLock lock(&device_->mutex_);
        value->u16 = device_->num_queues_;
        value->access_size = 2;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS: {
        fbl::AutoLock lock(&device_->mutex_);
        value->u8 = device_->status_;
        value->access_size = 1;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SEL: {
        fbl::AutoLock lock(&device_->mutex_);
        value->u16 = device_->queue_sel_;
        value->access_size = 2;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE: {
        virtio_queue_t* queue = selected_queue();
        if (queue == nullptr)
            return ZX_ERR_BAD_STATE;

        fbl::AutoLock lock(&queue->mutex);
        value->u16 = queue->size;
        value->access_size = 2;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE:
        // Virtio 1.0 Section 4.1.4.3: The device MUST present a 0 in
        // queue_enable on reset.
        //
        // Note the implementation currently does not respect this value.
        value->access_size = 2;
        value->u16 = 0;
        return ZX_OK;
    case VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW... VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH: {
        virtio_queue_t* queue = selected_queue();
        if (queue == nullptr)
            return ZX_ERR_BAD_STATE;

        size_t word = (addr - VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW) / sizeof(uint32_t);
        fbl::AutoLock lock(&queue->mutex);
        value->u32 = queue->addr.words[word];
        value->access_size = 4;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF: {
        fbl::AutoLock lock(&device_->mutex_);
        if (device_->queue_sel_ >= device_->num_queues_)
            return ZX_ERR_BAD_STATE;

        value->u32 = device_->queue_sel_;
        value->access_size = 4;
        return ZX_OK;
    }

    // Currently not implmeneted.
    case VIRTIO_PCI_COMMON_CFG_CONFIG_GEN:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR:
    case VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG:
        value->u32 = 0;
        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::ConfigBarRead(uint64_t addr, IoValue* value) {
    switch (addr) {
    case kVirtioPciCommonCfgBase... kVirtioPciCommonCfgTop:
        return CommonCfgRead(addr - kVirtioPciCommonCfgBase, value);
    case kVirtioPciIsrCfgBase... kVirtioPciIsrCfgTop:
        fbl::AutoLock lock(&device_->mutex_);
        value->u8 = device_->isr_status_;
        value->access_size = 1;

        // From VIRTIO 1.0 Section 4.1.4.5:
        //
        // To avoid an extra access, simply reading this register resets it to
        // 0 and causes the device to de-assert the interrupt.
        device_->isr_status_ = 0;
        return ZX_OK;
    }

    size_t device_config_top = kVirtioPciDeviceCfgBase + device_->device_config_size_;
    if (addr >= kVirtioPciDeviceCfgBase && addr < device_config_top) {
        uint64_t device_offset = addr - kVirtioPciDeviceCfgBase;
        return device_->ReadConfig(device_offset, value);
    }
    fprintf(stderr, "Unhandled read %#lx\n", addr);
    return ZX_ERR_NOT_SUPPORTED;
}

static void virtio_queue_update_addr(virtio_queue_t* queue) {
    virtio_queue_set_desc_addr(queue, queue->addr.desc);
    virtio_queue_set_avail_addr(queue, queue->addr.avail);
    virtio_queue_set_used_addr(queue, queue->addr.used);
}

/* Handle writes to the common configuration structure as defined in
 * Virtio 1.0 Section 4.1.4.3.
 */
zx_status_t VirtioPci::CommonCfgWrite(uint64_t addr, const IoValue& value) {
    switch (addr) {
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL: {
        if (value.access_size != 4)
            return ZX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device_->mutex_);
        device_->features_sel_ = value.u32;
        return ZX_OK;
    }

    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL: {
        if (value.access_size != 4)
            return ZX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device_->mutex_);
        device_->driver_features_sel_ = value.u32;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES: {
        if (value.access_size != 4)
            return ZX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device_->mutex_);
        if (device_->driver_features_sel_ == 0)
            device_->driver_features_ = value.u32;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS: {
        if (value.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;

        fbl::AutoLock lock(&device_->mutex_);
        device_->status_ = value.u8;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SEL: {
        if (value.access_size != 2)
            return ZX_ERR_IO_DATA_INTEGRITY;
        if (value.u16 >= device_->num_queues_)
            return ZX_ERR_NOT_SUPPORTED;

        fbl::AutoLock lock(&device_->mutex_);
        device_->queue_sel_ = value.u16;
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE: {
        if (value.access_size != 2)
            return ZX_ERR_IO_DATA_INTEGRITY;
        virtio_queue_t* queue = selected_queue();
        if (queue == nullptr)
            return ZX_ERR_BAD_STATE;

        fbl::AutoLock lock(&queue->mutex);
        queue->size = value.u16;
        virtio_queue_update_addr(queue);
        return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW... VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH: {
        if (value.access_size != 4)
            return ZX_ERR_IO_DATA_INTEGRITY;
        virtio_queue_t* queue = selected_queue();
        if (queue == nullptr)
            return ZX_ERR_BAD_STATE;

        size_t word = (addr - VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW) / sizeof(uint32_t);
        fbl::AutoLock lock(&queue->mutex);
        queue->addr.words[word] = value.u32;
        virtio_queue_update_addr(queue);
        return ZX_OK;
    }
    // Not implemented registers.
    case VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR:
    case VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE:
        return ZX_OK;
    // Read-only registers.
    case VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF:
    case VIRTIO_PCI_COMMON_CFG_NUM_QUEUES:
    case VIRTIO_PCI_COMMON_CFG_CONFIG_GEN:
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES:
        fprintf(stderr, "Unsupported write to %#lx\n", addr);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::ConfigBarWrite(uint64_t addr, const IoValue& value) {
    switch (addr) {
    case kVirtioPciCommonCfgBase... kVirtioPciCommonCfgTop: {
        uint64_t offset = addr - kVirtioPciCommonCfgBase;
        return CommonCfgWrite(offset, value);
    }
    }

    size_t device_config_top = kVirtioPciDeviceCfgBase + device_->device_config_size_;
    if (addr >= kVirtioPciDeviceCfgBase && addr < device_config_top) {
        uint64_t device_offset = addr - kVirtioPciDeviceCfgBase;
        return device_->WriteConfig(device_offset, value);
    }
    fprintf(stderr, "Unhandled write %#lx\n", addr);
    return ZX_ERR_NOT_SUPPORTED;
}

void VirtioPci::SetupCap(pci_cap_t* cap, virtio_pci_cap_t* virtio_cap, uint8_t cfg_type,
                         size_t cap_len, size_t data_length, uint8_t bar, size_t bar_offset) {
    virtio_cap->cfg_type = cfg_type;
    virtio_cap->bar = bar;
    virtio_cap->offset = static_cast<uint32_t>(bar_offset);
    virtio_cap->length = static_cast<uint32_t>(data_length);

    cap->id = kPciCapTypeVendorSpecific;
    cap->data = reinterpret_cast<uint8_t*>(virtio_cap);
    cap->len = virtio_cap->cap_len = static_cast<uint8_t>(cap_len);
}

void VirtioPci::SetupCaps() {
    // Common configuration.
    SetupCap(&capabilities_[0], &common_cfg_cap_,
             VIRTIO_PCI_CAP_COMMON_CFG, sizeof(common_cfg_cap_),
             kVirtioPciCommonCfgSize, kVirtioPciBar, kVirtioPciCommonCfgBase);

    // Notify configuration.
    notify_cfg_cap_.notify_off_multiplier = kVirtioPciNotifyCfgMultiplier;
    size_t notify_size = device_->num_queues() * kVirtioPciNotifyCfgMultiplier;
    SetupCap(&capabilities_[1], &notify_cfg_cap_.cap,
             VIRTIO_PCI_CAP_NOTIFY_CFG, sizeof(notify_cfg_cap_),
             notify_size, kVirtioPciNotifyBar, kVirtioPciNotifyCfgBase);
    bar_[kVirtioPciNotifyBar].size = static_cast<uint32_t>(notify_size);
    bar_[kVirtioPciNotifyBar].trap_type = TrapType::MMIO_BELL;

    // ISR configuration.
    SetupCap(&capabilities_[2], &isr_cfg_cap_,
             VIRTIO_PCI_CAP_ISR_CFG, sizeof(isr_cfg_cap_),
             kVirtioPciIsrCfgSize, kVirtioPciBar, kVirtioPciIsrCfgBase);

    // Device-specific configuration.
    SetupCap(&capabilities_[3], &device_cfg_cap_,
             VIRTIO_PCI_CAP_DEVICE_CFG, sizeof(device_cfg_cap_),
             device_->device_config_size_, kVirtioPciBar, kVirtioPciDeviceCfgBase);

    // Note VIRTIO_PCI_CAP_PCI_CFG is not implmeneted.
    // This one is more complex since it is writable and doesn't seem to be
    // used by Linux or Zircon.

    static_assert(kVirtioPciNumCapabilities == 4, "Incorrect number of capabilities.");
    set_capabilities(capabilities_, kVirtioPciNumCapabilities);

    static_assert(kVirtioPciBar < PCI_MAX_BARS, "Not enough BAR registers available.");
    bar_[kVirtioPciBar].size = static_cast<uint32_t>(
        kVirtioPciDeviceCfgBase + device_->device_config_size_);
    bar_[kVirtioPciBar].trap_type = TrapType::MMIO_SYNC;
}

static constexpr uint16_t virtio_pci_id(uint16_t virtio_id) {
    return static_cast<uint16_t>(virtio_id + 0x1040u);
}

virtio_queue_t* VirtioPci::selected_queue() {
    fbl::AutoLock lock(&device_->mutex_);
    if (device_->queue_sel_ >= device_->num_queues_)
        return nullptr;
    return &device_->queues_[device_->queue_sel_];
}

VirtioPci::VirtioPci(VirtioDevice* device)
    : PciDevice({
          .device_id = virtio_pci_id(device->device_id_),
          .vendor_id = kPciVendorIdVirtio,
          .subsystem_id = device->device_id_,
          .subsystem_vendor_id = 0,
          .class_code = 0,
          // Virtio 1.0 Section 4.1.2.1: Non-transitional devices SHOULD have a
          // PCI Revision ID of 1 or higher.
          .revision_id = 1,
      }),
      device_(device) {

    SetupCaps();
}

zx_status_t VirtioPci::ReadBar(uint8_t bar, uint64_t offset, IoValue* value) {
    switch (bar) {
    case kVirtioPciBar:
        return ConfigBarRead(offset, value);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::WriteBar(uint8_t bar, uint64_t offset, const IoValue& value) {
    switch (bar) {
    case kVirtioPciBar:
        return ConfigBarWrite(offset, value);
    case kVirtioPciNotifyBar:
        return NotifyBarWrite(offset, value);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::NotifyBarWrite(uint64_t offset, const IoValue& value) {
    if (!is_aligned(offset, kVirtioPciNotifyCfgMultiplier))
        return ZX_ERR_INVALID_ARGS;

    uint64_t notify_queue = offset / kVirtioPciNotifyCfgMultiplier;
    if (notify_queue >= device_->num_queues())
        return ZX_ERR_INVALID_ARGS;

    return device_->Kick(static_cast<uint16_t>(notify_queue));
}
