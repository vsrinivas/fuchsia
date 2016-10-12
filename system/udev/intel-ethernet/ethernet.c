// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/ethernet.h>
#include <hw/pci.h>

#include <magenta/listnode.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef mx_status_t status_t;
#include "ie.h"

#define INTERVAL 10000000000ULL

typedef struct ethernet_device ethernet_device_t;

struct ethernet_device {
    ethdev_t eth;
    mtx_t lock;
    mx_device_t dev;
    pci_protocol_t* pci;
    mx_device_t* pcidev;
    mx_handle_t ioh;
    mx_handle_t irqh;
    bool edge_triggered_irq;
    thrd_t thread;
};

#define get_eth_device(d) containerof(d, ethernet_device_t, dev)

static int irq_thread(void* arg) {
    ethernet_device_t* edev = arg;
    for (;;) {
        mx_status_t r;
        if ((r = mx_handle_wait_one(edev->irqh, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL)) < 0) {
            printf("eth: irq wait failed? %d\n", r);
            mx_interrupt_complete(edev->irqh);
            break;
        }

        if (edev->edge_triggered_irq)
            mx_interrupt_complete(edev->irqh);

        mtx_lock(&edev->lock);
        if (eth_handle_irq(&edev->eth) & ETH_IRQ_RX) {
            device_state_set(&edev->dev, DEV_STATE_READABLE);
        }
        mtx_unlock(&edev->lock);

        if (!edev->edge_triggered_irq)
            mx_interrupt_complete(edev->irqh);
    }
    return 0;
}

static mx_status_t eth_recv(mx_device_t* dev, void* data, size_t len) {
    ethernet_device_t* edev = get_eth_device(dev);
    mx_status_t r = ERR_BAD_STATE;
    mtx_lock(&edev->lock);
    r = eth_rx(&edev->eth, data);
    if (r <= 0) {
        device_state_clr(dev, DEV_STATE_READABLE);
    }
    mtx_unlock(&edev->lock);
    return r;
}

static mx_status_t eth_send(mx_device_t* dev, const void* data, size_t len) {
    ethernet_device_t* edev = get_eth_device(dev);
    if (len > ETH_TXBUF_DSIZE) {
        return ERR_INVALID_ARGS;
    }
    mx_status_t r = len;
    mtx_lock(&edev->lock);
    r = eth_tx(&edev->eth, data, len);
    mtx_unlock(&edev->lock);
    return r;
}

static mx_status_t eth_get_mac_addr(mx_device_t* dev, uint8_t* out_addr) {
    ethernet_device_t* edev = get_eth_device(dev);
    memcpy(out_addr, edev->eth.mac, sizeof(edev->eth.mac));
    return NO_ERROR;
}

static bool eth_is_online(mx_device_t* dev) {
    return true;
}

static size_t eth_get_mtu(mx_device_t* dev) {
    return ETH_RXBUF_SIZE;
}

static ethernet_protocol_t ethernet_ops = {
    .send = eth_send,
    .recv = eth_recv,
    .get_mac_addr = eth_get_mac_addr,
    .is_online = eth_is_online,
    .get_mtu = eth_get_mtu,
};

// simplified read/write interface

static ssize_t eth_read(mx_device_t* dev, void* data, size_t len, mx_off_t off) {
    // special case reading MAC address
    if (len == ETH_MAC_SIZE) {
        eth_get_mac_addr(dev, data);
        return len;
    }
    if (len < eth_get_mtu(dev)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    return eth_recv(dev, data, len);
}

static ssize_t eth_write(mx_device_t* dev, const void* data, size_t len, mx_off_t off) {
    return eth_send(dev, data, len);
}

static mx_status_t eth_release(mx_device_t* dev) {
    ethernet_device_t* edev = get_eth_device(dev);
    eth_reset_hw(&edev->eth);
    edev->pci->enable_bus_master(edev->pcidev, true);
    mx_handle_close(edev->irqh);
    mx_handle_close(edev->ioh);
    free(dev);
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t device_ops = {
    .release = eth_release,
    .read = eth_read,
    .write = eth_write,
};

static mx_status_t eth_bind(mx_driver_t* drv, mx_device_t* dev) {
    ethernet_device_t* edev;
    if ((edev = calloc(1, sizeof(ethernet_device_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    mtx_init(&edev->lock, mtx_plain);

    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci)) {
        printf("no pci protocol\n");
        goto fail;
    }
    edev->pcidev = dev;
    edev->pci = pci;

    mx_status_t r;
    if ((r = pci->claim_device(dev)) < 0) {
        return r;
    }

    if (pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_MSI, 1)) {
        if (pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_LEGACY, 1)) {
            printf("eth: failed to set irq mode\n");
            goto fail;
        } else {
            printf("eth: using legacy irq mode\n");
            edev->edge_triggered_irq = false;
        }
    } else {
        edev->edge_triggered_irq = true;
    }

    if ((edev->irqh = pci->map_interrupt(dev, 0)) < 0) {
        printf("eth: failed to map irq\n");
        goto fail;
    }

    // map iomem
    uint64_t sz;
    mx_handle_t h;
    void *io;
    if ((h = pci->map_mmio(dev, 0, MX_CACHE_POLICY_UNCACHED_DEVICE, &io, &sz)) < 0) {
        printf("eth: cannot map io %d\n", h);
        goto fail;
    }
    edev->eth.iobase = (uintptr_t) io;
    edev->ioh = h;

    if ((r = pci->enable_bus_master(dev, true)) < 0) {
        printf("eth: cannot enable bus master %d\n", r);
        goto fail;
    }

    if (eth_reset_hw(&edev->eth)) {
        goto fail;
    }

    mx_paddr_t iophys;
    void* iomem;
    if ((r = mx_alloc_device_memory(get_root_resource(), ETH_ALLOC, &iophys, &iomem)) < 0) {
        printf("eth: cannot alloc buffers %d\n", r);
        goto fail;
    }

    eth_setup_buffers(&edev->eth, iomem, iophys);
    eth_init_hw(&edev->eth);

    device_init(&edev->dev, drv, "intel-ethernet", &device_ops);
    edev->dev.protocol_id = MX_PROTOCOL_ETHERNET;
    edev->dev.protocol_ops = &ethernet_ops;
    if (device_add(&edev->dev, dev)) {
        goto fail;
    }

    thrd_create_with_name(&edev->thread, irq_thread, edev, "eth-irq-thread");
    thrd_detach(edev->thread);

    return NO_ERROR;

fail:
    if (edev->ioh) {
        edev->pci->enable_bus_master(edev->pcidev, true);
        mx_handle_close(edev->irqh);
        mx_handle_close(edev->ioh);
    }
    free(edev);
    return ERR_NOT_SUPPORTED;
}

mx_driver_t _driver_intel_ethernet = {
    .ops = {
        .bind = eth_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_intel_ethernet, "intel-ethernet", "magenta", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x100E), // Qemu
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15A3), // Broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1570), // Skylake
MAGENTA_DRIVER_END(_driver_intel_ethernet)
