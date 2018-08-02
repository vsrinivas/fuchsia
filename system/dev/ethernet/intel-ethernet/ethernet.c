// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <zircon/assert.h>
#include <zircon/device/ethernet.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef zx_status_t status_t;
#include "ie.h"

typedef enum {
    ETH_RUNNING = 0,
    ETH_SUSPENDING,
    ETH_SUSPENDED,
} eth_state;

typedef struct ethernet_device {
    ethdev_t eth;
    mtx_t lock;
    eth_state state;
    zx_device_t* zxdev;
    pci_protocol_t pci;
    zx_handle_t ioh;
    zx_handle_t irqh;
    thrd_t thread;
    zx_handle_t btih;
    io_buffer_t buffer;
    bool online;

    // callback interface to attached ethernet layer
    ethmac_ifc_t* ifc;
    void* cookie;
} ethernet_device_t;

static int irq_thread(void* arg) {
    ethernet_device_t* edev = arg;
    for (;;) {
        zx_status_t r;
        r = zx_interrupt_wait(edev->irqh, NULL);
        if (r != ZX_OK) {
            printf("eth: irq wait failed? %d\n", r);
            break;
        }
        mtx_lock(&edev->lock);
        unsigned irq = eth_handle_irq(&edev->eth);
        if (irq & ETH_IRQ_RX) {
            void* data;
            size_t len;

            while (eth_rx(&edev->eth, &data, &len) == ZX_OK) {
                if (edev->ifc && (edev->state == ETH_RUNNING)) {
                    edev->ifc->recv(edev->cookie, data, len, 0);
                }
                eth_rx_ack(&edev->eth);
            }
        }
        if (irq & ETH_IRQ_LSC) {
            bool was_online = edev->online;
            bool online = eth_status_online(&edev->eth);
            zxlogf(TRACE, "intel-eth: ETH_IRQ_LSC fired: %d->%d\n", was_online, online);
            if (online != was_online) {
                edev->online = online;
                if (edev->ifc) {
                    edev->ifc->status(edev->cookie, online ? ETH_STATUS_ONLINE : 0);
                }
            }
        }
        mtx_unlock(&edev->lock);
    }
    return 0;
}

static zx_status_t eth_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    ethernet_device_t* edev = ctx;

    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    ZX_DEBUG_ASSERT(ETH_TXBUF_SIZE >= ETH_MTU);
    info->mtu = ETH_MTU;
    memcpy(info->mac, edev->eth.mac, sizeof(edev->eth.mac));

    return ZX_OK;
}

static void eth_stop(void* ctx) {
    ethernet_device_t* edev = ctx;
    mtx_lock(&edev->lock);
    edev->ifc = NULL;
    mtx_unlock(&edev->lock);
}

static zx_status_t eth_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    ethernet_device_t* edev = ctx;
    zx_status_t status = ZX_OK;

    mtx_lock(&edev->lock);
    if (edev->ifc) {
        status = ZX_ERR_BAD_STATE;
    } else {
        edev->ifc = ifc;
        edev->cookie = cookie;
        edev->ifc->status(edev->cookie, edev->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&edev->lock);

    return status;
}

static zx_status_t eth_queue_tx(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) {
    ethernet_device_t* edev = ctx;
    if (edev->state != ETH_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }
    // TODO: Add support for DMA directly from netbuf
    return eth_tx(&edev->eth, netbuf->data, netbuf->len);
}

static zx_status_t eth_set_param(void *ctx, uint32_t param, int32_t value, void* data) {
    ethernet_device_t* edev = ctx;
    zx_status_t status = ZX_OK;

    mtx_lock(&edev->lock);

    switch (param) {
    case ETHMAC_SETPARAM_PROMISC:
        if ((bool)value) {
            eth_start_promisc(&edev->eth);
        } else {
            eth_stop_promisc(&edev->eth);
        }
        status = ZX_OK;
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
    }
    mtx_unlock(&edev->lock);

    return status;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = eth_query,
    .stop = eth_stop,
    .start = eth_start,
    .queue_tx = eth_queue_tx,
    .set_param = eth_set_param,
};

static zx_status_t eth_suspend(void* ctx, uint32_t flags) {
    ethernet_device_t* edev = ctx;
    mtx_lock(&edev->lock);
    edev->state = ETH_SUSPENDING;

    // Immediately disable the rx queue
    eth_disable_rx(&edev->eth);

    // Wait for queued tx packets to complete
    int iterations = 0;
    do {
        if (!eth_tx_queued(&edev->eth)) {
            goto tx_done;
        }
        mtx_unlock(&edev->lock);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
        iterations++;
        mtx_lock(&edev->lock);
    } while (iterations < 10);
    printf("intel-eth: timed out waiting for tx queue to drain when suspending\n");

tx_done:
    eth_disable_tx(&edev->eth);
    eth_disable_phy(&edev->eth);
    edev->state = ETH_SUSPENDED;
    mtx_unlock(&edev->lock);
    return ZX_OK;
}

static zx_status_t eth_resume(void* ctx, uint32_t flags) {
    ethernet_device_t* edev = ctx;
    mtx_lock(&edev->lock);
    eth_enable_phy(&edev->eth);
    eth_enable_rx(&edev->eth);
    eth_enable_tx(&edev->eth);
    edev->state = ETH_RUNNING;
    mtx_unlock(&edev->lock);
    return ZX_OK;
}

static void eth_release(void* ctx) {
    ethernet_device_t* edev = ctx;
    eth_reset_hw(&edev->eth);
    pci_enable_bus_master(&edev->pci, false);

    io_buffer_release(&edev->buffer);

    zx_handle_close(edev->btih);
    zx_handle_close(edev->irqh);
    zx_handle_close(edev->ioh);
    free(edev);
}

static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .suspend = eth_suspend,
    .resume = eth_resume,
    .release = eth_release,
};

static zx_status_t eth_bind(void* ctx, zx_device_t* dev) {
    ethernet_device_t* edev;
    if ((edev = calloc(1, sizeof(ethernet_device_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    mtx_init(&edev->lock, mtx_plain);
    mtx_init(&edev->eth.send_lock, mtx_plain);

    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &edev->pci)) {
        printf("no pci protocol\n");
        goto fail;
    }

    zx_status_t status = pci_get_bti(&edev->pci, 0, &edev->btih);
    if (status != ZX_OK) {
        goto fail;
    }

    // Query whether we have MSI or Legacy interrupts.
    uint32_t irq_cnt = 0;
    if ((pci_query_irq_mode(&edev->pci, ZX_PCIE_IRQ_MODE_MSI, &irq_cnt) == ZX_OK) &&
        (pci_set_irq_mode(&edev->pci, ZX_PCIE_IRQ_MODE_MSI, 1) == ZX_OK)) {
        printf("eth: using MSI mode\n");
    } else if ((pci_query_irq_mode(&edev->pci, ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt) == ZX_OK) &&
               (pci_set_irq_mode(&edev->pci, ZX_PCIE_IRQ_MODE_LEGACY, 1) == ZX_OK)) {
        printf("eth: using legacy irq mode\n");
    } else {
        printf("eth: failed to configure irqs\n");
        goto fail;
    }

    zx_status_t r = pci_map_interrupt(&edev->pci, 0, &edev->irqh);
    if (r != ZX_OK) {
        printf("eth: failed to map irq\n");
        goto fail;
    }

    // map iomem
    uint64_t sz;
    zx_handle_t h;
    void* io;
    r = pci_map_bar(&edev->pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &io, &sz, &h);
    if (r != ZX_OK) {
        printf("eth: cannot map io %d\n", h);
        goto fail;
    }
    edev->eth.iobase = (uintptr_t)io;
    edev->ioh = h;

    zx_pcie_device_info_t pci_info;
    status = pci_get_device_info(&edev->pci, &pci_info);
    if (status != ZX_OK) {
        goto fail;
    }
    edev->eth.pci_did = pci_info.device_id;

    if ((r = pci_enable_bus_master(&edev->pci, true)) < 0) {
        printf("eth: cannot enable bus master %d\n", r);
        goto fail;
    }

    if (eth_enable_phy(&edev->eth) != ZX_OK) {
        goto fail;
    }

    if (eth_reset_hw(&edev->eth)) {
        goto fail;
    }

    r = io_buffer_init(&edev->buffer, edev->btih, ETH_ALLOC, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (r < 0) {
        printf("eth: cannot alloc io-buffer %d\n", r);
        goto fail;
    }

    eth_setup_buffers(&edev->eth, io_buffer_virt(&edev->buffer), io_buffer_phys(&edev->buffer));
    eth_init_hw(&edev->eth);
    edev->online = eth_status_online(&edev->eth);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "intel-ethernet",
        .ctx = edev,
        .ops = &device_ops,
        .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
        .proto_ops = &ethmac_ops,
    };

    if (device_add(dev, &args, &edev->zxdev)) {
        goto fail;
    }

    thrd_create_with_name(&edev->thread, irq_thread, edev, "eth-irq-thread");
    thrd_detach(edev->thread);

    printf("eth: intel-ethernet online\n");

    return ZX_OK;

fail:
    io_buffer_release(&edev->buffer);
    if (edev->btih) {
        zx_handle_close(edev->btih);
    }
    if (edev->ioh) {
        pci_enable_bus_master(&edev->pci, false);
        zx_handle_close(edev->irqh);
        zx_handle_close(edev->ioh);
    }
    free(edev);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_driver_ops_t intel_ethernet_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = eth_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(intel_ethernet, intel_ethernet_driver_ops, "zircon", "0.1", 13)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x100E), // Qemu
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1f45), // Atom c2000 2.5Gbe backplane
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1502), // Ivy Bridge
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15A3), // Broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1570), // Skylake
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1533), // I210 standalone
    BI_MATCH_IF(EQ, BIND_PCI_DID, IE_DID_I211_AT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, IE_DID_I219_LM),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b7), // Skull Canyon NUC
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b8), // I219-V
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15d8), // Kaby Lake NUC
ZIRCON_DRIVER_END(intel_ethernet)
