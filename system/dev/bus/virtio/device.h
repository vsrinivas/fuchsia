// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <zx/handle.h>
#include <fbl/mutex.h>
#include <threads.h>
#include <virtio/virtio.h>

namespace virtio {

class Device {
public:
    Device(zx_device_t* bus_device);
    virtual ~Device();

    zx_device_t* bus_device() { return bus_device_; }
    zx_device_t* device() { return device_; }

    virtual zx_status_t Bind(pci_protocol_t*, zx_handle_t pci_config_handle, const pci_config_t*);
    virtual zx_status_t Init() = 0;
    virtual void Unbind();
    virtual void Release();

    void StartIrqThread();

    // interrupt cases that devices may override
    virtual void IrqRingUpdate() {}
    virtual void IrqConfigChange() {}

    // used by Ring class to manipulate config registers
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail, zx_paddr_t pa_used);
    uint16_t GetRingSize(uint16_t index);
    void RingKick(uint16_t ring_index);

protected:
    // read bytes out of BAR 0's config space
    template <typename T> T ReadConfigBar(uint16_t offset);
    template <typename T> void WriteConfigBar(uint16_t offset, T val);
    zx_status_t CopyDeviceConfig(void* _buf, size_t len);
    template <typename T> void WriteDeviceConfig(uint16_t offset, T val);

    void Reset();
    void StatusAcknowledgeDriver();
    void StatusDriverOK();

    static int IrqThreadEntry(void* arg);
    void IrqWorker();

    zx_status_t MapBar(uint8_t bar);

    // members
    zx_device_t* bus_device_ = nullptr;
    fbl::Mutex lock_;

    // handles to pci bits
    pci_protocol_t pci_ = { nullptr, nullptr };
    zx::handle pci_config_handle_ = {};
    const pci_config_t* pci_config_ = nullptr;
    zx::handle irq_handle_ = {};

    // bar0 memory map or PIO
    uint32_t bar0_pio_base_ = 0;
    uint32_t bar0_size_ = 0; // for now, must be set in subclass before Bind()

    // based on the capability descriptions multiple bars may need to be mapped
    struct bar {
        volatile void* mmio_base;
        zx::handle mmio_handle;
    } bar_[6] = {};
    struct {
        volatile virtio_pci_common_cfg* common_config;
        volatile uint32_t* isr_status;
        volatile uint16_t* notify_base;
        uint32_t notify_mul;
        volatile void* device_config;
    } mmio_regs_ = {};

    // irq thread object
    thrd_t irq_thread_ = {};

    // DDK device
    zx_device_t* device_ = nullptr;
    zx_protocol_device_t device_ops_ = {};
};

} // namespace virtio
