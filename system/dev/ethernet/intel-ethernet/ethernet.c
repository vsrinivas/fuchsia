// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <magenta/device/ethernet.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef mx_status_t status_t;
#include "ie.h"

typedef struct ethernet_device {
    ethdev_t eth;
    mtx_t lock;
    mx_device_t* mxdev;
    pci_protocol_t pci;
    mx_handle_t ioh;
    mx_handle_t irqh;
    bool edge_triggered_irq;
    thrd_t thread;
    io_buffer_t buffer;
    bool online;

    // callback interface to attached ethernet layer
    ethmac_ifc_t* ifc;
    void* cookie;
} ethernet_device_t;

static int irq_thread(void* arg) {
    ethernet_device_t* edev = arg;
    for (;;) {
        mx_status_t r;
        if ((r = mx_interrupt_wait(edev->irqh)) < 0) {
            printf("eth: irq wait failed? %d\n", r);
            mx_interrupt_complete(edev->irqh);
            break;
        }

        if (edev->edge_triggered_irq)
            mx_interrupt_complete(edev->irqh);

        mtx_lock(&edev->lock);
        unsigned irq = eth_handle_irq(&edev->eth);
        if (irq & ETH_IRQ_RX) {
            void* data;
            size_t len;

            while (eth_rx(&edev->eth, &data, &len) == MX_OK) {
                if (edev->ifc) {
                    edev->ifc->recv(edev->cookie, data, len, 0);
                }
                eth_rx_ack(&edev->eth);
            }
        }
        if (irq & ETH_IRQ_LSC) {
            bool was_online = edev->online;
            bool online = eth_status_online(&edev->eth);
            if (online != was_online) {
                edev->online = online;
                if (edev->ifc) {
                    edev->ifc->status(edev->cookie, online ? ETH_STATUS_ONLINE : 0);
                }
            }

        }
        mtx_unlock(&edev->lock);

        if (!edev->edge_triggered_irq)
            mx_interrupt_complete(edev->irqh);
    }
    return 0;
}

static mx_status_t eth_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    ethernet_device_t* edev = ctx;

    if (options) {
        return MX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = ETH_RXBUF_SIZE; //TODO: not actually the mtu!
    memcpy(info->mac, edev->eth.mac, sizeof(edev->eth.mac));

    return MX_OK;
}

static void eth_stop(void* ctx) {
    ethernet_device_t* edev = ctx;
    mtx_lock(&edev->lock);
    edev->ifc = NULL;
    mtx_unlock(&edev->lock);
}

static mx_status_t eth_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    ethernet_device_t* edev = ctx;
    mx_status_t status = MX_OK;

    mtx_lock(&edev->lock);
    if (edev->ifc) {
        status = MX_ERR_BAD_STATE;
    } else {
        edev->ifc = ifc;
        edev->cookie = cookie;
        edev->ifc->status(edev->cookie, edev->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&edev->lock);

    return status;
}

static void eth_send(void* ctx, uint32_t options, void* data, size_t length) {
    ethernet_device_t* edev = ctx;
    eth_tx(&edev->eth, data, length);
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = eth_query,
    .stop = eth_stop,
    .start = eth_start,
    .send = eth_send,
};

static void eth_release(void* ctx) {
    ethernet_device_t* edev = ctx;
    eth_reset_hw(&edev->eth);
    pci_enable_bus_master(&edev->pci, true);
    mx_handle_close(edev->irqh);
    mx_handle_close(edev->ioh);
    free(edev);
}

static mx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .release = eth_release,
};

static mx_status_t eth_bind(void* ctx, mx_device_t* dev, void** cookie) {
    ethernet_device_t* edev;
    if ((edev = calloc(1, sizeof(ethernet_device_t))) == NULL) {
        return MX_ERR_NO_MEMORY;
    }
    mtx_init(&edev->lock, mtx_plain);
    mtx_init(&edev->eth.send_lock, mtx_plain);

    if (device_get_protocol(dev, MX_PROTOCOL_PCI, &edev->pci)) {
        printf("no pci protocol\n");
        goto fail;
    }

    // Query whether we have MSI or Legacy interrupts.
    uint32_t irq_cnt = 0;
    if ((pci_query_irq_mode_caps(&edev->pci, MX_PCIE_IRQ_MODE_MSI, &irq_cnt) == MX_OK) &&
        (pci_set_irq_mode(&edev->pci, MX_PCIE_IRQ_MODE_MSI, 1) == MX_OK)) {
        edev->edge_triggered_irq = true;
        printf("eth: using MSI mode\n");
    } else if ((pci_query_irq_mode_caps(&edev->pci, MX_PCIE_IRQ_MODE_LEGACY, &irq_cnt) == MX_OK) &&
               (pci_set_irq_mode(&edev->pci, MX_PCIE_IRQ_MODE_LEGACY, 1) == MX_OK)) {
        edev->edge_triggered_irq = false;
        printf("eth: using legacy irq mode\n");
    } else {
        printf("eth: failed to configure irqs\n");
        goto fail;
    }

    mx_status_t r = pci_map_interrupt(&edev->pci, 0, &edev->irqh);
    if (r != MX_OK) {
        printf("eth: failed to map irq\n");
        goto fail;
    }

    // map iomem
    uint64_t sz;
    mx_handle_t h;
    void* io;
    r = pci_map_resource(&edev->pci, PCI_RESOURCE_BAR_0, MX_CACHE_POLICY_UNCACHED_DEVICE, &io, &sz, &h);
    if (r != MX_OK) {
        printf("eth: cannot map io %d\n", h);
        goto fail;
    }
    edev->eth.iobase = (uintptr_t)io;
    edev->ioh = h;

    if ((r = pci_enable_bus_master(&edev->pci, true)) < 0) {
        printf("eth: cannot enable bus master %d\n", r);
        goto fail;
    }

    if (eth_reset_hw(&edev->eth)) {
        goto fail;
    }

    r = io_buffer_init(&edev->buffer, ETH_ALLOC, IO_BUFFER_RW);
    if (r < 0) {
        printf("eth: cannot alloc io-buffer %d\n", r);
        goto fail;
    }

    eth_setup_buffers(&edev->eth, io_buffer_virt(&edev->buffer), io_buffer_phys(&edev->buffer));
    eth_init_hw(&edev->eth);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "intel-ethernet",
        .ctx = edev,
        .ops = &device_ops,
        .proto_id = MX_PROTOCOL_ETHERMAC,
        .proto_ops = &ethmac_ops,
    };

    if (device_add(dev, &args, &edev->mxdev)) {
        goto fail;
    }

    thrd_create_with_name(&edev->thread, irq_thread, edev, "eth-irq-thread");
    thrd_detach(edev->thread);

    printf("eth: intel-ethernet online\n");

    return MX_OK;

fail:
    io_buffer_release(&edev->buffer);
    if (edev->ioh) {
        pci_enable_bus_master(&edev->pci, true);
        mx_handle_close(edev->irqh);
        mx_handle_close(edev->ioh);
    }
    free(edev);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_driver_ops_t intel_ethernet_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = eth_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(intel_ethernet, intel_ethernet_driver_ops, "magenta", "0.1", 9)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x100E), // Qemu
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15A3), // Broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1570), // Skylake
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1533), // I210 standalone
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b7), // Skull Canyon NUC
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b8), // I219
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15d8), // Kaby Lake NUC
MAGENTA_DRIVER_END(intel_ethernet)
