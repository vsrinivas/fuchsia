// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hw/inout.h>
#include <magenta/status.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>

#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

Device::Device(mx_device_t* bus_device)
    : bus_device_(bus_device) {
    LTRACE_ENTRY;
    device_ops_.version = DEVICE_OPS_VERSION;
}

Device::~Device() {
    LTRACE_ENTRY;
}

mx_status_t Device::MapBar(uint8_t i) {
    if (bar_[i].mmio_handle != MX_HANDLE_INVALID)
        return MX_OK;

    uint64_t sz;
    mx_handle_t tmp_handle;

    mx_status_t r = pci_map_resource(&pci_, PCI_RESOURCE_BAR_0 + i, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                     (void**)&bar_[i].mmio_base, &sz, &tmp_handle);
    if (r != MX_OK) {
        VIRTIO_ERROR("cannot map io %d\n", bar_[i].mmio_handle.get());
        return r;
    }
    bar_[i].mmio_handle.reset(tmp_handle);
    LTRACEF("bar %hhu mmio_base %p, sz %#" PRIx64 "\n", i, bar_[i].mmio_base, sz);

    return MX_OK;
}

mx_status_t Device::Bind(pci_protocol_t* pci,
                         mx_handle_t pci_config_handle, const pci_config_t* pci_config) {
    LTRACE_ENTRY;

    fbl::AutoLock lock(&lock_);
    mx_handle_t tmp_handle;

    // save off handles to things
    memcpy(&pci_, pci, sizeof(pci_protocol_t));
    pci_config_handle_.reset(pci_config_handle);
    pci_config_ = pci_config;

    // enable bus mastering
    mx_status_t r;
    if ((r = pci_enable_bus_master(&pci_, true)) != MX_OK) {
        VIRTIO_ERROR("cannot enable bus master %d\n", r);
        return r;
    }

    // try to set up our IRQ mode
    if (pci_set_irq_mode(&pci_, MX_PCIE_IRQ_MODE_MSI, 1)) {
        if (pci_set_irq_mode(&pci_, MX_PCIE_IRQ_MODE_LEGACY, 1)) {
            VIRTIO_ERROR("failed to set irq mode\n");
            return -1;
        } else {
            LTRACEF("using legacy irq mode\n");
        }
    }

    r = pci_map_interrupt(&pci_, 0, &tmp_handle);
    if (r != MX_OK) {
        VIRTIO_ERROR("failed to map irq %d\n", r);
        return r;
    }
    irq_handle_.reset(tmp_handle);

    LTRACEF("irq handle %u\n", irq_handle_.get());

    // try to parse capabilities
    if (pci_config_->status & PCI_STATUS_NEW_CAPS) {
        LTRACEF("pci config capabilities_ptr 0x%x\n", pci_config_->capabilities_ptr);

        size_t off = pci_config_->capabilities_ptr;
        for (int i = 0; i < 64; i++) { // only loop so many times in case things out of whack
            virtio_pci_cap *cap;

            if (off > PAGE_SIZE) {
                VIRTIO_ERROR("capability pointer is out of whack %zu\n", off);
                return MX_ERR_INVALID_ARGS;
            }

            cap = (virtio_pci_cap *)(((uintptr_t)pci_config_) + off);
            LTRACEF("cap %p: type %#hhx next %#hhx len %#hhx cfg_type %#hhx bar %#hhx offset %#x length %#x\n",
                    cap, cap->cap_vndr, cap->cap_next, cap->cap_len, cap->cfg_type, cap->bar, cap->offset, cap->length);

            if (cap->cap_vndr == 0x9) { // vendor specific capability
                switch (cap->cfg_type) {
                    case VIRTIO_PCI_CAP_COMMON_CFG: {
                        MapBar(cap->bar);
                        mmio_regs_.common_config = (volatile virtio_pci_common_cfg*)((uintptr_t)bar_[cap->bar].mmio_base + cap->offset);
                        LTRACEF("common_config %p\n", mmio_regs_.common_config);
                        break;
                    }
                    case VIRTIO_PCI_CAP_NOTIFY_CFG: {
                        MapBar(cap->bar);
                        mmio_regs_.notify_base = (volatile uint16_t*)((uintptr_t)bar_[cap->bar].mmio_base + cap->offset);
                        LTRACEF("notify_base %p\n", mmio_regs_.notify_base);
                        mmio_regs_.notify_mul = ((virtio_pci_notify_cap *) cap)->notify_off_multiplier;
                        LTRACEF("notify_mul %x\n", mmio_regs_.notify_mul);
                        break;
                    }
                    case VIRTIO_PCI_CAP_ISR_CFG: {
                        MapBar(cap->bar);
                        mmio_regs_.isr_status = (volatile uint32_t*)((uintptr_t)bar_[cap->bar].mmio_base + cap->offset);
                        LTRACEF("isr_status %p\n", mmio_regs_.isr_status);
                        break;
                    }
                    case VIRTIO_PCI_CAP_DEVICE_CFG: {
                        MapBar(cap->bar);
                        mmio_regs_.device_config = (volatile void*)((uintptr_t)bar_[cap->bar].mmio_base + cap->offset);
                        LTRACEF("device_config %p\n", mmio_regs_.device_config);
                        break;
                    }
                    case VIRTIO_PCI_CAP_PCI_CFG: {
                        // will be pointing at bar0, which we'll map below anyway
                        break;
                    }
                }
            }

            off = cap->cap_next;
            if (cap->cap_next == 0)
                break;
        }
    }

    // if we've found mmio pointers to everything from the capability structure, then skip mapping bar0, since we don't
    // need legacy pio access from BAR0
    if (!(mmio_regs_.common_config && mmio_regs_.notify_base && mmio_regs_.isr_status && mmio_regs_.device_config)) {
        // transitional devices have a single PIO window at BAR0
        if (pci_config_->base_addresses[0] & 0x1) {
            // look at BAR0, which should be a PIO memory window
            bar0_pio_base_ = pci_config->base_addresses[0];
            LTRACEF("BAR0 address %#x\n", bar0_pio_base_);
            if ((bar0_pio_base_ & 0x1) == 0) {
                VIRTIO_ERROR("bar 0 does not appear to be PIO (address %#x, aborting\n", bar0_pio_base_);
                return -1;
            }

            bar0_pio_base_ &= ~1;
            if (bar0_pio_base_ > 0xffff) {
                bar0_pio_base_ = 0;

                r = MapBar(0);
                if (r != MX_OK) {
                    VIRTIO_ERROR("cannot mmap io %d\n", r);
                    return r;
                }

                LTRACEF("bar_[0].mmio_base %p\n", bar_[0].mmio_base);
            } else {
                // this is probably PIO
                r = mx_mmap_device_io(get_root_resource(), bar0_pio_base_, bar0_size_);
                if (r != MX_OK) {
                    VIRTIO_ERROR("failed to access PIO range %#x, length %#xw\n", bar0_pio_base_, bar0_size_);
                    return r;
                }
            }

            // enable pio access
            if ((r = pci_enable_pio(&pci_, true)) < 0) {
                VIRTIO_ERROR("cannot enable PIO %d\n", r);
                return -1;
            }
        }
    }

    LTRACE_EXIT;

    return MX_OK;
}

void Device::Unbind() {
    device_remove(device_);
}

void Device::Release() {
    irq_handle_.reset();
}

void Device::IrqWorker() {
    LTRACEF("started\n");
    mx_status_t rc;
    assert(irq_handle_);

    while (irq_handle_) {
        if ((rc = mx_interrupt_wait(irq_handle_.get())) < 0) {
            printf("virtio: error while waiting for interrupt: %s\n",
                mx_status_get_string(rc));
            continue;
        }

        uint32_t irq_status;
        if (mmio_regs_.isr_status) {
            irq_status = *mmio_regs_.isr_status;
        } else {
            irq_status = inp((bar0_pio_base_ + VIRTIO_PCI_ISR_STATUS) & 0xffff);
        }

        LTRACEF_LEVEL(2, "irq_status %#x\n", irq_status);

        if ((rc = mx_interrupt_complete(irq_handle_.get())) < 0) {
            printf("virtio: error while completing interrupt: %s\n",
                mx_status_get_string(rc));
            continue;
        }

        if (irq_status == 0)
            continue;

        // grab the mutex for the duration of the irq handlers
        fbl::AutoLock lock(&lock_);

        if (irq_status & 0x1) { /* used ring update */
            IrqRingUpdate();
        }
        if (irq_status & 0x2) { /* config change */
            IrqConfigChange();
        }
    }
}

int Device::IrqThreadEntry(void* arg) {
    Device* d = static_cast<Device*>(arg);

    d->IrqWorker();

    return 0;
}

void Device::StartIrqThread() {
    thrd_create_with_name(&irq_thread_, IrqThreadEntry, this, "virtio-irq-thread");
    thrd_detach(irq_thread_);
}

namespace {
template <typename T> T ioread(uint16_t port);

template<> uint8_t ioread<uint8_t>(uint16_t port) { return inp(port); }
template<> uint16_t ioread<uint16_t>(uint16_t port) { return inpw(port); }
template<> uint32_t ioread<uint32_t>(uint16_t port) { return inpd(port); }

template <typename T> void iowrite(uint16_t port, T val);

template<> void iowrite<uint8_t>(uint16_t port, uint8_t val) { return outp(port, val); }
template<> void iowrite<uint16_t>(uint16_t port, uint16_t val) { return outpw(port, val); }
template<> void iowrite<uint32_t>(uint16_t port, uint32_t val) { return outpd(port, val); }
} // anon namespace

template <typename T>
T Device::ReadConfigBar(uint16_t offset) {
    if (bar0_pio_base_) {
        uint16_t port = (bar0_pio_base_ + offset) & 0xffff;
        LTRACEF_LEVEL(3, "port %#x\n", port);
        return ioread<T>(port);
    } else if (bar_[0].mmio_base) {
        volatile T *addr = (volatile T *)((uintptr_t)bar_[0].mmio_base + offset);
        LTRACEF_LEVEL(3, "addr %p\n", addr);
        return *addr;
    } else {
        // XXX implement
        assert(0);
        return 0;
    }
}

template <typename T>
void Device::WriteConfigBar(uint16_t offset, T val) {
    if (bar0_pio_base_) {
        uint16_t port = (bar0_pio_base_ + offset) & 0xffff;
        LTRACEF_LEVEL(3, "port %#x\n", port);
        iowrite<T>(port, val);
    } else if (bar_[0].mmio_base) {
        volatile T *addr = (volatile T *)((uintptr_t)bar_[0].mmio_base + offset);
        LTRACEF_LEVEL(3, "addr %p\n", addr);
        *addr = val;
    } else {
        // XXX implement
        assert(0);
    }
}

mx_status_t Device::CopyDeviceConfig(void* _buf, size_t len) {
    if (mmio_regs_.device_config) {
        memcpy(_buf, (void *)mmio_regs_.device_config, len);
    } else {
        // XXX handle MSI vs noMSI
        size_t offset = VIRTIO_PCI_CONFIG_OFFSET_NOMSI;

        uint8_t* buf = (uint8_t*)_buf;
        for (size_t i = 0; i < len; i++) {
            buf[i] = ReadConfigBar<uint8_t>((offset + i) & 0xffff);
        }
    }

    return MX_OK;
}

uint16_t Device::GetRingSize(uint16_t index) {
    if (!mmio_regs_.common_config) {
        if (bar0_pio_base_) {
            return inpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_SIZE) & 0xffff);
        } else if (bar_[0].mmio_base) {
            volatile uint16_t *ptr16 = (volatile uint16_t *)((uintptr_t)bar_[0].mmio_base + VIRTIO_PCI_QUEUE_SIZE);
            return *ptr16;
        } else {
            // XXX implement
            assert(0);
            return 0;
        }
    } else {
        mmio_regs_.common_config->queue_select = index;
        return mmio_regs_.common_config->queue_size;
    }
}

void Device::SetRing(uint16_t index, uint16_t count, mx_paddr_t pa_desc, mx_paddr_t pa_avail, mx_paddr_t pa_used) {
    LTRACEF("index %u, count %u, pa_desc %#" PRIxPTR ", pa_avail %#" PRIxPTR ", pa_used %#" PRIxPTR "\n",
            index, count, pa_desc, pa_avail, pa_used);

    if (!mmio_regs_.common_config) {
        if (bar0_pio_base_) {
            outpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_SELECT) & 0xffff, index);
            outpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_SIZE) & 0xffff, count);
            outpd((bar0_pio_base_ + VIRTIO_PCI_QUEUE_PFN) & 0xffff, (uint32_t)(pa_desc / PAGE_SIZE));
        } else if (bar_[0].mmio_base) {
            volatile uint16_t *ptr16 = (volatile uint16_t *)((uintptr_t)bar_[0].mmio_base + VIRTIO_PCI_QUEUE_SELECT);
            *ptr16 = index;
            ptr16 = (volatile uint16_t *)((uintptr_t)bar_[0].mmio_base + VIRTIO_PCI_QUEUE_SIZE);
            *ptr16 = count;
            volatile uint32_t *ptr32 = (volatile uint32_t *)((uintptr_t)bar_[0].mmio_base + VIRTIO_PCI_QUEUE_PFN);
            *ptr32 = (uint32_t)(pa_desc / PAGE_SIZE);
        } else {
            // XXX implement
            assert(0);
        }
    } else {
        mmio_regs_.common_config->queue_select = index;
        mmio_regs_.common_config->queue_size = count;
        mmio_regs_.common_config->queue_desc = pa_desc;
        mmio_regs_.common_config->queue_avail = pa_avail;
        mmio_regs_.common_config->queue_used = pa_used;
        mmio_regs_.common_config->queue_enable = 1;
    }
}

void Device::RingKick(uint16_t ring_index) {
    LTRACEF("index %u\n", ring_index);
    if (!mmio_regs_.notify_base) {
        if (bar0_pio_base_) {
            outpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_NOTIFY) & 0xffff, ring_index);
        } else {
            // XXX implement
            assert(0);
        }
    } else {
        volatile uint16_t* notify = mmio_regs_.notify_base + ring_index * mmio_regs_.notify_mul / sizeof(uint16_t);
        LTRACEF_LEVEL(2, "notify address %p\n", notify);
        *notify = ring_index;
    }
}

void Device::Reset() {
    if (!mmio_regs_.common_config) {
        WriteConfigBar<uint8_t>(VIRTIO_PCI_DEVICE_STATUS, 0);
    } else {
        mmio_regs_.common_config->device_status = 0;
    }
}

void Device::StatusAcknowledgeDriver() {
    if (!mmio_regs_.common_config) {
        uint8_t val = ReadConfigBar<uint8_t>(VIRTIO_PCI_DEVICE_STATUS);
        val |= VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
        WriteConfigBar(VIRTIO_PCI_DEVICE_STATUS, val);
    } else {
        mmio_regs_.common_config->device_status |= VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    }
}

void Device::StatusDriverOK() {
    if (!mmio_regs_.common_config) {
        uint8_t val = ReadConfigBar<uint8_t>(VIRTIO_PCI_DEVICE_STATUS);
        val |= VIRTIO_STATUS_DRIVER_OK;
        WriteConfigBar(VIRTIO_PCI_DEVICE_STATUS, val);
    } else {
        mmio_regs_.common_config->device_status |= VIRTIO_STATUS_DRIVER_OK;
    }
}

} // namespace virtio
