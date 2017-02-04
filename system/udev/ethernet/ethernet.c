// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/ethernet.h>
#include <eth/eth-fifo.h>

#include <magenta/device/ethernet.h>
#include <magenta/listnode.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// ethernet device
typedef struct ethdev0 {
    // shared state
    mx_device_t* mac;
    ethmac_protocol_t* macops;

    mtx_t lock;

    // ethdev0 can only *really* be released
    // when it is removed *and* all the instances
    // (which depend on it for locking and global
    // state) are gone.
    uint32_t refcount;

    // active and idle instances (ethdev_t)
    list_node_t list_active;
    list_node_t list_idle;

    ethmac_info_t info;

    mx_device_t dev;
} ethdev0_t;

static void eth0_downref(ethdev0_t* edev0) {
    mtx_lock(&edev0->lock);
    edev0->refcount--;
    if (edev0->refcount == 0) {
        mtx_unlock(&edev0->lock);
        free(edev0);
    } else {
        mtx_unlock(&edev0->lock);
    }
}

// transmit thread has been created
#define ETHDEV_TX_THREAD (1u)

// connected to the ethmac and handling traffic
#define ETHDEV_RUNNING (2u)

// being destroyed
#define ETHDEV_DEAD (4u)

// ethernet instance device
typedef struct ethdev {
    list_node_t node;

    ethdev0_t* edev0;

    uint32_t state;

    // fifos
    eth_fifo_t fifo;
    eth_fifo_entry_t* rx_entries;
    eth_fifo_entry_t* tx_entries;

    // Buffer VMO
    mx_handle_t io_vmo;

    // Buffer mappings and sizes
    uint8_t* rx_map;
    uint8_t* tx_map;
    uint32_t rx_size;
    uint32_t tx_size;

    // fifo thread
    thrd_t tx_thr;

    mx_device_t dev;
} ethdev_t;

#define get_ethdev(d) containerof(d, ethdev_t, dev)
#define get_ethdev0(d) containerof(d, ethdev0_t, dev)

static void eth_handle_rx(ethdev_t* edev, void* data, size_t len) {
    mx_status_t status;
    mx_fifo_state_t state;

    if ((status = mx_fifo_op(edev->fifo.rx_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        printf("%s could not read rx fifo state (%d)\n", __func__, status);
        return;
    }
    if (state.head == state.tail) {
        // No space in fifo, drop packet
        return;
    }

    uint64_t idx = state.tail & (edev->fifo.rx_entries_count - 1);
    eth_fifo_entry_t* entry = &edev->rx_entries[idx];

    uint32_t eoff = entry->offset;
    uint32_t elen = entry->length;
    if ((eoff >= edev->rx_size) || ((elen > (edev->rx_size - eoff)))) {
        // invalid offset/length. report error. drop packet
        entry->length = 0;
        entry->flags = ETH_FIFO_INVALID;
    } else if (len > elen) {
        // packet does not fit. drop it
        return;
    } else {
        // packet fits. deliver it
        memcpy(edev->rx_map + eoff, data, len);
        entry->length = len;
        entry->flags = ETH_FIFO_RX_OK;
    }

    if ((status = mx_fifo_op(edev->fifo.rx_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, &state)) < 0) {
        printf("%s could not advance rx fifo tail (%d)\n", __func__, status);
        // TODO: probably fatal - stop transport?
    }
}

static void eth0_status(void* cookie, uint32_t status) {
    printf("eth: status() %08x\n", status);
}

static void eth0_recv(void* cookie, void* data, size_t len, uint32_t flags) {
    ethdev0_t* edev0 = cookie;

    ethdev_t* edev;
    mtx_lock(&edev0->lock);
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        eth_handle_rx(edev, data, len);
    }
    mtx_unlock(&edev0->lock);
}

static ethmac_ifc_t ethmac_ifc = {
    .status = eth0_status,
    .recv = eth0_recv,
};

static int eth_tx_thread(void* arg) {
    ethdev_t* edev = (ethdev_t*)arg;

    mx_fifo_state_t state;
    mx_status_t status;

    mx_device_t* mac = edev->edev0->mac;
    ethmac_protocol_t* macops = edev->edev0->macops;

    for (;;) {
        if ((status = mx_handle_wait_one(edev->fifo.tx_fifo, MX_FIFO_NOT_EMPTY, MX_TIME_INFINITE, NULL)) < 0) {
            break;
        }
        if ((status = mx_fifo_op(edev->fifo.tx_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
            break;
        }

        uint64_t num_fifo_entries = state.head - state.tail;
        while (num_fifo_entries > 0) {
            uint64_t idx = state.tail & (edev->fifo.tx_entries_count - 1);
            eth_fifo_entry_t* entry = &edev->tx_entries[idx];

            uint32_t eoff = entry->offset;
            uint32_t elen = entry->length;
            if ((eoff >= edev->tx_size) || ((elen > (edev->tx_size - eoff)))) {
                entry->length = 0;
                entry->flags = ETH_FIFO_INVALID;
            } else {
                macops->send(mac, 0, edev->tx_map + eoff, elen);
                entry->flags = ETH_FIFO_TX_OK;
            }

            // TODO: batch these up
            if ((status = mx_fifo_op(edev->fifo.tx_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, &state)) < 0) {
                break;
            }
            num_fifo_entries = state.head - state.tail;
        }
    }

    printf("eth: tx_thread: exit: %d\n", status);
    return 0;
}

static ssize_t eth_get_fifo_locked(ethdev_t* edev, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len) {
    if ((in_len < sizeof(eth_get_fifo_args_t)) ||
        (out_len < sizeof(eth_fifo_t))) {
        return ERR_INVALID_ARGS;
    }
    // For now, we can only have one fifo per instance
    if (edev->fifo.entries_vmo != MX_HANDLE_INVALID) {
        return ERR_ALREADY_BOUND;
    }

    const eth_get_fifo_args_t* args = in_buf;
    eth_fifo_t* reply = out_buf;

    // Create the fifo
    mx_status_t status = eth_fifo_create(args->rx_entries, args->tx_entries,
                                         args->options, &edev->fifo);
    if (status != NO_ERROR) {
        printf("eth: failed to create eth_fifo (%d)\n", status);
        return status;
    }

    // Map rx and tx entries
    status = eth_fifo_map_rx_entries(&edev->fifo, &edev->rx_entries);
    if (status != NO_ERROR) {
        printf("eth: failed to map rx fifo entries: %d\n", status);
        goto map_rx_entries_fail;
    }
    status = eth_fifo_map_tx_entries(&edev->fifo, &edev->tx_entries);
    if (status != NO_ERROR) {
        printf("eth: failed to map tx fifo entries: %d\n", status);
        goto map_tx_entries_fail;
    }

    // Set up the caller's copy
    status = eth_fifo_clone_producer(&edev->fifo, reply);
    if (status != NO_ERROR) {
        printf("eth: failed to clone producer fifo: %d\n", status);
        goto clone_producer_fail;
    }

    return sizeof(*reply);

clone_producer_fail:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)edev->tx_entries, 0);
map_tx_entries_fail:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)edev->rx_entries, 0);
map_rx_entries_fail:
    eth_fifo_cleanup(&edev->fifo);
    return status;
}

static ssize_t eth_set_io_buf_locked(ethdev_t* edev, const void* in_buf, size_t in_len) {
    if (in_len < sizeof(eth_set_io_buf_args_t)) {
        return ERR_INVALID_ARGS;
    }
    if (edev->io_vmo != MX_HANDLE_INVALID) {
        return ERR_ALREADY_BOUND;
    }

    const eth_set_io_buf_args_t* args = in_buf;
    edev->io_vmo = args->io_buf_vmo;

    if ((args->rx_len > (128*1024*1024)) ||
        (args->tx_len > (128*1024*1024))) {
        return ERR_INVALID_ARGS;
    }

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

    edev->rx_size = (uint32_t) args->rx_len;
    edev->tx_size = (uint32_t) args->tx_len;
    return NO_ERROR;

map_tx_failed:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)edev->rx_map, 0);
map_rx_failed:
    mx_handle_close(edev->io_vmo);
    edev->io_vmo = MX_HANDLE_INVALID;
    return status;
}

static mx_status_t eth_start_locked(ethdev_t* edev) {
    ethdev0_t* edev0 = edev->edev0;

    // Cannot start unless tx/rx rings are configured
    if ((edev->io_vmo == MX_HANDLE_INVALID) ||
        (edev->fifo.entries_vmo == MX_HANDLE_INVALID)) {
        return ERR_BAD_STATE;
    }

    if (edev->state & ETHDEV_RUNNING) {
        return NO_ERROR;
    }

    if (!(edev->state & ETHDEV_TX_THREAD)) {
        int r = thrd_create_with_name(&edev->tx_thr, eth_tx_thread,
                                      edev, "eth-tx-thread");
        if (r != thrd_success) {
            printf("eth: failed to start tx thread: %d\n", r);
            return ERR_INTERNAL;
        }
        edev->state |= ETHDEV_TX_THREAD;
    }

    mx_status_t status;
    if (list_is_empty(&edev0->list_active)) {
        status = edev0->macops->start(edev0->mac, &ethmac_ifc, edev0);
    } else {
        status = NO_ERROR;
    }

    if (status == NO_ERROR) {
        edev->state |= ETHDEV_RUNNING;
        list_delete(&edev->node);
        list_add_tail(&edev0->list_active, &edev->node);
    } else {
        printf("eth: failed to start mac: %d\n", status);
    }

    return status;
}

static mx_status_t eth_stop_locked(ethdev_t* edev) {
    ethdev0_t* edev0 = edev->edev0;

    if (edev->state & ETHDEV_RUNNING) {
        edev->state &= (~ETHDEV_RUNNING);
        list_delete(&edev->node);
        list_add_tail(&edev0->list_idle, &edev->node);
        if (list_is_empty(&edev0->list_active)) {
            if (!(edev->state & ETHDEV_DEAD)) {
                edev0->macops->stop(edev0->mac);
            }
        }
    }

    return NO_ERROR;
}

static ssize_t eth_ioctl(mx_device_t* dev, uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len) {

    ethdev_t* edev = get_ethdev(dev);
    mtx_lock(&edev->edev0->lock);
    mx_status_t status;
    if (edev->state & ETHDEV_DEAD) {
        status = ERR_BAD_STATE;
        goto done;
    }

    switch (op) {
    case IOCTL_ETHERNET_GET_MAC_ADDR: {
        if (out_len < ETH_MAC_SIZE) {
            status = ERR_BUFFER_TOO_SMALL;
        } else {
            memcpy(out_buf, edev->edev0->info.mac, ETH_MAC_SIZE);
            status = ETH_MAC_SIZE;
        }
        break;
    }
    case IOCTL_ETHERNET_GET_MTU: {
        size_t* mtu = out_buf;
        if (out_len < sizeof(*mtu)) {
            status = ERR_BUFFER_TOO_SMALL;
        } else {
            *mtu = edev->edev0->info.mtu;
            status = sizeof(*mtu);
        }
        break;
    }
    case IOCTL_ETHERNET_GET_FIFO:
        status = eth_get_fifo_locked(edev, in_buf, in_len, out_buf, out_len);
        break;
    case IOCTL_ETHERNET_SET_IO_BUF:
        status = eth_set_io_buf_locked(edev, in_buf, in_len);
        break;
    case IOCTL_ETHERNET_START:
        status = eth_start_locked(edev);
        break;
    case IOCTL_ETHERNET_STOP:
        status = eth_stop_locked(edev);
        break;
    default:
        status = ERR_NOT_SUPPORTED;
    }

done:
    mtx_unlock(&edev->edev0->lock);

    return status;
}

// kill tx thread, release buffers, etc
// called from unbind and close
static void eth_kill_locked(ethdev_t* edev) {
    if (edev->state & ETHDEV_DEAD) {
        return;
    }

    // make sure any future ioctls or other ops will fail
    edev->state |= ETHDEV_DEAD;

    // try to convince clients to close us
    if (edev->fifo.rx_fifo) {
        mx_fifo_op(edev->fifo.rx_fifo, MX_FIFO_OP_CONSUMER_EXCEPTION, 1, NULL);
        mx_fifo_op(edev->fifo.tx_fifo, MX_FIFO_OP_CONSUMER_EXCEPTION, 1, NULL);
    }
    if (edev->io_vmo) {
        mx_handle_close(edev->io_vmo);
        edev->io_vmo = MX_HANDLE_INVALID;
    }
    eth_fifo_cleanup(&edev->fifo);
    if (edev->state & ETHDEV_TX_THREAD) {
        edev->state &= (~ETHDEV_TX_THREAD);
        int ret;
        thrd_join(edev->tx_thr, &ret);
    }

    //TODO: unmap memory *after* thread exits
}

static mx_status_t eth_release(mx_device_t* dev) {
    ethdev_t* edev = get_ethdev(dev);
    eth0_downref(edev->edev0);
    free(edev);
    return ERR_NOT_SUPPORTED;
}

static mx_status_t eth_close(mx_device_t* dev, uint32_t flags) {
    ethdev_t* edev = get_ethdev(dev);

    mtx_lock(&edev->edev0->lock);
    eth_stop_locked(edev);
    eth_kill_locked(edev);
    list_delete(&edev->node);
    mtx_unlock(&edev->edev0->lock);

    return NO_ERROR;
}

static mx_protocol_device_t ethdev_ops = {
    .close = eth_close,
    .ioctl = eth_ioctl,
    .release = eth_release,
};

static ethernet_protocol_t ethernet_ops = {};

extern mx_driver_t _driver_ethernet;

static mx_status_t eth0_open(mx_device_t* dev, mx_device_t** out, uint32_t flags) {
    ethdev0_t* edev0 = get_ethdev0(dev);

    ethdev_t* edev;
    if ((edev = calloc(1, sizeof(ethdev_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    device_init(&edev->dev, &_driver_ethernet, "ethernet", &ethdev_ops);
    edev->dev.protocol_id = MX_PROTOCOL_ETHERNET;
    edev->dev.protocol_ops = &ethernet_ops;
    edev->edev0 = edev0;

    mx_status_t status;
    if ((status = device_add_instance(&edev->dev, dev)) < 0) {
        free(edev);
        return status;
    }

    mtx_lock(&edev0->lock);
    edev0->refcount++;
    list_add_tail(&edev0->list_idle, &edev->node);
    mtx_unlock(&edev0->lock);

    *out = &edev->dev;
    return NO_ERROR;
}

static void eth0_unbind(mx_device_t* dev) {
    ethdev0_t* edev0 = get_ethdev0(dev);

    mtx_lock(&edev0->lock);

    // tear down shared memory, fifos, and threads
    // to encourage any open instances to close
    ethdev_t* edev;
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        eth_kill_locked(edev);
    }
    list_for_every_entry(&edev0->list_idle, edev, ethdev_t, node) {
        eth_kill_locked(edev);
    }

    mtx_unlock(&edev0->lock);

    device_remove(dev);
}

static mx_status_t eth0_release(mx_device_t* dev) {
    ethdev0_t* edev0 = get_ethdev0(dev);
    eth0_downref(edev0);
    return NO_ERROR;
}

static mx_protocol_device_t ethdev0_ops = {
    .open = eth0_open,
    .unbind = eth0_unbind,
    .release = eth0_release,
};


#define BAD_FEATURES (ETHMAC_FEATURE_RX_QUEUE | ETHMAC_FEATURE_TX_QUEUE)

static mx_status_t eth_bind(mx_driver_t* drv, mx_device_t* dev) {
    ethdev0_t* edev0;
    if ((edev0 = calloc(1, sizeof(ethdev0_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status;
    if (device_get_protocol(dev, MX_PROTOCOL_ETHERMAC, (void**)&edev0->macops)) {
        printf("no ethermac protocol\n");
        status = ERR_INTERNAL;
        goto fail;
    }

    if ((status = edev0->macops->query(dev, 0, &edev0->info)) < 0) {
        printf("ethermac query failed: %d\n", status);
        goto fail;
    }

    if (edev0->info.features & BAD_FEATURES) {
        printf("ethermac requires unsupported features: %08x\n",
               edev0->info.features & BAD_FEATURES);
        status = ERR_NOT_SUPPORTED;
        goto fail;
    }

    device_init(&edev0->dev, drv, "ethernet", &ethdev0_ops);
    mtx_init(&edev0->lock, mtx_plain);
    list_initialize(&edev0->list_active);
    list_initialize(&edev0->list_idle);

    // start with a reference that will live until release()
    edev0->refcount = 1;

    edev0->mac = dev;
    edev0->dev.protocol_id = MX_PROTOCOL_ETHERNET;

    if ((status = device_add(&edev0->dev, dev)) < 0) {
        goto fail;
    }

    return NO_ERROR;

fail:
    free(edev0);
    return status;
}

mx_driver_t _driver_ethernet = {
    .ops = {
        .bind = eth_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_ethernet, "ethernet", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ETHERMAC),
MAGENTA_DRIVER_END(_driver_ethernet)
