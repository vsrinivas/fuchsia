// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/ethernet.h>
#include <eth/eth-fifo.h>
#include <hw/pci.h>

#include <magenta/device/ethernet.h>
#include <magenta/listnode.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef mx_status_t status_t;
#include "ie.h"

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
    io_buffer_t buffer;

    // fifos
    eth_fifo_t fifo;
    eth_fifo_entry_t* rx_entries;
    eth_fifo_entry_t* tx_entries;
    // Buffer VMO
    mx_handle_t io_vmo;
    // Buffer mappings
    uint8_t* rx_map;
    uint8_t* tx_map;
    // fifo threads
    thrd_t tx_thr;
};

#define get_eth_device(d) containerof(d, ethernet_device_t, dev)

static void eth_handle_rx(ethernet_device_t* edev);

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
        if (eth_handle_irq(&edev->eth) & ETH_IRQ_RX) {
            eth_handle_rx(edev);
            device_state_set(&edev->dev, DEV_STATE_READABLE);
        }
        mtx_unlock(&edev->lock);

        if (!edev->edge_triggered_irq)
            mx_interrupt_complete(edev->irqh);
    }
    return 0;
}

static mx_status_t eth_get_mac_addr(mx_device_t* dev, uint8_t* out_addr) {
    ethernet_device_t* edev = get_eth_device(dev);
    memcpy(out_addr, edev->eth.mac, sizeof(edev->eth.mac));
    return NO_ERROR;
}

static size_t eth_get_mtu(mx_device_t* dev) {
    return ETH_RXBUF_SIZE;
}

static void eth_handle_rx(ethernet_device_t* edev) {
    mx_status_t status;
    mx_fifo_state_t state;

    if (edev->fifo.rx_fifo == MX_HANDLE_INVALID) {
        // No client has established a fifo, so drop the packet
        eth_rx(&edev->eth, NULL);
        return;
    }

    status = mx_fifo_op(edev->fifo.rx_fifo, MX_FIFO_OP_READ_STATE, 0, &state);
    if (status != NO_ERROR) {
        printf("%s could not read rx fifo state (%d)\n", __func__, status);
        return;
    }
    if (state.head == state.tail) {
        eth_rx(&edev->eth, NULL);
        return;
    }
    uint64_t num_fifo_entries = state.head - state.tail;
    while (num_fifo_entries > 0) {
        uint64_t entry_idx = state.tail & (edev->fifo.rx_entries_count - 1);
        eth_fifo_entry_t* entry = &edev->rx_entries[entry_idx];
        status = eth_rx(&edev->eth, &edev->rx_map[entry->offset]);
        if (status == ERR_SHOULD_WAIT) {
            // Done processing rx
            break;
        }
        if (status < 0) {
            printf("eth: could not read packet: %d\n", status);
            break;
        }
        entry->length = status;
        // TODO: batch up fifo ops
        mx_status_t status = mx_fifo_op(edev->fifo.rx_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, &state);
        if (status != NO_ERROR) {
            printf("%s could not advance rx fifo tail (%d)\n", __func__, status);
            // TODO: figure out what to do in this case. Current logic will
            // drop the packet that we just received, which seems fine for
            // now.
        }
        num_fifo_entries = state.head - state.tail;
    }
}

static int eth_tx_thread(void* arg) {
    ethernet_device_t* edev = (ethernet_device_t*)arg;

    mx_fifo_state_t state;
    mx_status_t status;
    while (true) {
        do {
            status = mx_handle_wait_one(edev->fifo.tx_fifo, MX_FIFO_NOT_EMPTY, MX_SEC(1), NULL);
            // TODO: deal with unbind/release for intel-ethernet
        } while (status == ERR_TIMED_OUT);
        if (status != NO_ERROR) {
            printf("%s handle wait for tx fifo failed (%d)\n", __func__, status);
            break;
        }

        status = mx_fifo_op(edev->fifo.tx_fifo, MX_FIFO_OP_READ_STATE, 0, &state);
        if (status != NO_ERROR) {
            printf("%s could not read tx fifo state (%d)\n", __func__, status);
            break;
        }

        uint64_t num_fifo_entries = state.head - state.tail;
        while (num_fifo_entries > 0) {
            uint64_t entry_idx = state.tail & (edev->fifo.tx_entries_count - 1);
            eth_fifo_entry_t* entry = &edev->tx_entries[entry_idx];

            status = eth_tx(&edev->eth, &edev->tx_map[entry->offset], entry->length);
            if (status < 0) {
                printf("%s could not sent packet: %d\n", __func__, status);
            }

            // TODO: batch these up
            mx_status_t status = mx_fifo_op(edev->fifo.tx_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, &state);
            if (status != NO_ERROR) {
                printf("%s could not advance tx fifo tail (%d)\n", __func__, status);
            }
            num_fifo_entries = state.head - state.tail;
        }
    }
    return 0;
}

static ethernet_protocol_t ethernet_ops = {};

static ssize_t eth_get_fifo(ethernet_device_t* edev, const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len) {
    if (in_len < sizeof(eth_get_fifo_args_t) || !in_buf) return ERR_INVALID_ARGS;
    if (out_len < sizeof(eth_fifo_t) || !out_buf) return ERR_INVALID_ARGS;
    // For now, we can only have one fifo per instance
    if (edev->fifo.entries_vmo != MX_HANDLE_INVALID) return ERR_ALREADY_BOUND;

    const eth_get_fifo_args_t* args = in_buf;
    eth_fifo_t* reply = out_buf;

    // Create the fifo
    eth_fifo_t fifo;
    mx_status_t status = eth_fifo_create(args->rx_entries, args->tx_entries, args->options,
        &fifo);
    if (status != NO_ERROR) {
        printf("failed to create eth_fifo (%d)\n", status);
        eth_fifo_cleanup(&fifo);
        return status;
    }

    // Set up the driver's copy
    status = eth_fifo_clone_consumer(&fifo, &edev->fifo);
    if (status != NO_ERROR) {
        printf("failed to clone consumer fifo: %d\n", status);
        goto clone_consumer_fail;
    }
    status = eth_fifo_map_rx_entries(&edev->fifo, &edev->rx_entries);
    if (status != NO_ERROR) {
        printf("failed to map rx fifo entries: %d\n", status);
        goto map_rx_entries_fail;
    }
    status = eth_fifo_map_tx_entries(&edev->fifo, &edev->tx_entries);
    if (status != NO_ERROR) {
        printf("failed to map tx fifo entries: %d\n", status);
        goto map_tx_entries_fail;
    }

    // Set up the caller's copy
    status = eth_fifo_clone_producer(&fifo, reply);
    if (status != NO_ERROR) {
        printf("failed to clone producer fifo: %d\n", status);
        goto clone_producer_fail;
    }

    // Spawn the tx thread
    int ret = thrd_create_with_name(&edev->tx_thr, eth_tx_thread, edev, "eth_tx_thread");
    if (ret != thrd_success) {
        printf("failed to start tx thread: %d\n", ret);
        status = ERR_BAD_STATE;
        goto tx_thread_fail;
    }

    thrd_detach(edev->tx_thr);

    return sizeof(*reply);

tx_thread_fail:
    eth_fifo_cleanup(reply);
clone_producer_fail:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)edev->tx_entries, 0);
map_tx_entries_fail:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)edev->rx_entries, 0);
map_rx_entries_fail:
    eth_fifo_cleanup(&edev->fifo);
clone_consumer_fail:
    eth_fifo_cleanup(&fifo);
    return status;
}

static ssize_t eth_set_io_buf(ethernet_device_t* edev, const void* in_buf, size_t in_len) {
    if (in_len < sizeof(eth_set_io_buf_args_t) || !in_buf) return ERR_INVALID_ARGS;

    const eth_set_io_buf_args_t* args = in_buf;
    edev->io_vmo = args->io_buf_vmo;

    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, edev->io_vmo, args->rx_offset,
            args->rx_len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)&edev->rx_map);
    if (status != NO_ERROR) {
        printf("eth: could not map rx buffer: %d\n", status);
        goto map_rx_failed;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, edev->io_vmo, args->tx_offset,
            args->tx_len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)&edev->tx_map);
    if (status != NO_ERROR) {
        printf("eth: could not map tx buffer: %d\n", status);
        goto map_tx_failed;
    }
    return NO_ERROR;

map_tx_failed:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)edev->rx_map, 0);
map_rx_failed:
    mx_handle_close(edev->io_vmo);
    edev->io_vmo = MX_HANDLE_INVALID;
    return status;
}

static ssize_t eth_ioctl(mx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len,
        void* out_buf, size_t out_len) {
    ethernet_device_t* edev = get_eth_device(dev);
    switch (op) {
    case IOCTL_ETHERNET_GET_MAC_ADDR: {
        uint8_t* mac = out_buf;
        if (out_len < ETH_MAC_SIZE) return ERR_BUFFER_TOO_SMALL;
        eth_get_mac_addr(dev, mac);
        return ETH_MAC_SIZE;
    }
    case IOCTL_ETHERNET_GET_MTU: {
        size_t* mtu = out_buf;
        if (out_len < sizeof(*mtu)) return ERR_BUFFER_TOO_SMALL;
        *mtu = eth_get_mtu(dev);
        return sizeof(*mtu);
    }
    case IOCTL_ETHERNET_GET_FIFO:
        return eth_get_fifo(edev, in_buf, in_len, out_buf, out_len);
    case IOCTL_ETHERNET_SET_IO_BUF:
        return eth_set_io_buf(edev, in_buf, in_len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t eth_release(mx_device_t* dev) {
    ethernet_device_t* edev = get_eth_device(dev);
    eth_reset_hw(&edev->eth);
    edev->pci->enable_bus_master(edev->pcidev, true);
    mx_handle_close(edev->irqh);
    mx_handle_close(edev->ioh);
    // TODO: clean up fifo and threads
    free(dev);
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t device_ops = {
    .ioctl = eth_ioctl,
    .release = eth_release,
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

    // Query whether we have MSI or Legacy interrupts.
    uint32_t irq_cnt = 0;
    if ((pci->query_irq_mode_caps(dev, MX_PCIE_IRQ_MODE_MSI, &irq_cnt) == NO_ERROR) &&
            (pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_MSI, 1) == NO_ERROR)) {
        edev->edge_triggered_irq = true;
        printf("eth: using MSI mode\n");
    } else if ((pci->query_irq_mode_caps(dev, MX_PCIE_IRQ_MODE_LEGACY, &irq_cnt) == NO_ERROR) &&
               (pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_LEGACY, 1) == NO_ERROR)) {
        edev->edge_triggered_irq = false;
        printf("eth: using legacy irq mode\n");
    } else {
        printf("eth: failed to configure irqs\n");
        goto fail;
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

    r = io_buffer_init(&edev->buffer, ETH_ALLOC, IO_BUFFER_RW);
    if (r < 0) {
        printf("eth: cannot alloc io-buffer %d\n", r);
        goto fail;
    }

    eth_setup_buffers(&edev->eth, io_buffer_virt(&edev->buffer), io_buffer_phys(&edev->buffer));
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
    io_buffer_release(&edev->buffer);
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

MAGENTA_DRIVER_BEGIN(_driver_intel_ethernet, "intel-ethernet", "magenta", "0.1", 7)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x100E), // Qemu
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15A3), // Broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1570), // Skylake
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1533), // I210 standalone
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b8), // I219
MAGENTA_DRIVER_END(_driver_intel_ethernet)
