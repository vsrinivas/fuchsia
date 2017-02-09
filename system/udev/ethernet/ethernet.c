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

    // rx ioring
    // naming is client-centric
    // we read from enqueue, reply to dequeue
    eth_fifo_entry_t* rx_enqueue;
    eth_fifo_entry_t* rx_dequeue;
    mx_handle_t rx_enqueue_fifo;
    mx_handle_t rx_dequeue_fifo;
    uint32_t rx_size;
    uint32_t rx_mask;

    // tx ioring
    // naming is client-centric
    // we read from enqueue, reply to dequeue
    eth_fifo_entry_t* tx_enqueue;
    eth_fifo_entry_t* tx_dequeue;
    mx_handle_t tx_enqueue_fifo;
    mx_handle_t tx_dequeue_fifo;
    uint32_t tx_size;
    uint32_t tx_mask;

    // io buffer
    mx_handle_t io_vmo;
    void* io_buf;
    size_t io_size;

    // fifo thread
    thrd_t tx_thr;

    mx_device_t dev;
} ethdev_t;

#define get_ethdev(d) containerof(d, ethdev_t, dev)
#define get_ethdev0(d) containerof(d, ethdev0_t, dev)

static void eth_handle_rx(ethdev_t* edev, void* data, size_t len) {
    mx_status_t status;
    mx_fifo_state_t state;

    // look for a pending rx transaction we can complete
    if ((status = mx_fifo0_op(edev->rx_enqueue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        printf("eth: rx_enqueue_fifo: cannot read: %d\n", status);
        return;
    }
    if (state.head == state.tail) {
        printf("eth: rx_enqueue_fifo: empty!\n");
        return;
    }

    uint64_t idx = state.tail & edev->rx_mask;
    eth_fifo_entry_t* entry = edev->rx_enqueue + idx;

    uint32_t eoff = entry->offset;
    uint32_t elen = entry->length;
    void* ecookie = entry->cookie;
    uint16_t olen;
    uint16_t oflags;

    if ((eoff >= edev->io_size) || ((elen > (edev->io_size - eoff)))) {
        // invalid offset/length. report error. drop packet
        olen = 0;
        oflags = ETH_FIFO_INVALID;
    } else if (len > elen) {
        // packet does not fit. drop it
        return;
    } else {
        // packet fits. deliver it
        memcpy(edev->io_buf + eoff, data, len);
        olen = len;
        oflags = ETH_FIFO_RX_OK;
    }

    // look for space to write the completed transcation
    if ((status = mx_fifo0_op(edev->rx_dequeue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        printf("eth: rx_dequeue_fifo: cannot read: %d\n", status);
        return;
    }
    if ((state.head - state.tail) == edev->rx_size) {
        printf("eth: rx_dequeue_fifo: full!\n");
        return;
    }

    idx = state.head & edev->rx_mask;
    entry = edev->rx_dequeue + idx;

    entry->offset = eoff;
    entry->length = olen;
    entry->flags = oflags;
    entry->cookie = ecookie;

    // advance both fifos
    if ((status = mx_fifo0_op(edev->rx_dequeue_fifo, MX_FIFO_OP_ADVANCE_HEAD, 1u, NULL)) < 0) {
        printf("eth: rx_dwqueue_fifo: cannot advance head: %d\n", status);
    }
    if ((status = mx_fifo0_op(edev->rx_enqueue_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, NULL)) < 0) {
        printf("eth: rx_enqueue_fifo: cannot advance tail: %d\n", status);
    }
}

static mx_status_t eth_handle_tx(ethdev_t* edev) {
    mx_status_t status;
    mx_fifo_state_t state;

    // look for a pending tx transaction we can complete
    if ((status = mx_fifo0_op(edev->tx_enqueue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        printf("eth: tx_enqueue_fifo: cannot read: %d\n", status);
        return status;
    }
    if (state.head == state.tail) {
        printf("eth: tx_enqueue_fifo: empty!\n");
        return NO_ERROR;
    }

    uint64_t idx = state.tail & edev->tx_mask;
    eth_fifo_entry_t* entry = edev->tx_enqueue + idx;

    uint32_t eoff = entry->offset;
    uint32_t elen = entry->length;
    void* ecookie = entry->cookie;
    uint16_t oflags;

    if ((eoff >= edev->io_size) || ((elen > (edev->io_size - eoff)))) {
        // invalid offset/length. report error. drop packet
        oflags = ETH_FIFO_INVALID;
    } else {
        edev->edev0->macops->send(edev->edev0->mac, 0, edev->io_buf + eoff, elen);
        oflags = ETH_FIFO_TX_OK;
    }

    // look for space to write the completed transcation
    if ((status = mx_fifo0_op(edev->tx_dequeue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        printf("eth: tx_dequeue_fifo: cannot read: %d\n", status);
        return status;
    }
    if ((state.head - state.tail) == edev->tx_size) {
        printf("eth: tx_dequeue_fifo: full!\n");
        return status;
    }

    idx = state.head & edev->tx_mask;
    entry = edev->tx_dequeue + idx;

    entry->offset = eoff;
    entry->length = elen;
    entry->flags = oflags;
    entry->cookie = ecookie;

    // advance both fifos
    if ((status = mx_fifo0_op(edev->tx_dequeue_fifo, MX_FIFO_OP_ADVANCE_HEAD, 1u, NULL)) < 0) {
        printf("eth: tx_dequeue_fifo: cannot advance head: %d\n", status);
        return status;
    }
    if ((status = mx_fifo0_op(edev->tx_enqueue_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, NULL)) < 0) {
        printf("eth: tx_enqueue_fifo: cannot advance tail: %d\n", status);
        return status;
    }

    return NO_ERROR;
}


static void eth0_status(void* cookie, uint32_t status) {
    printf("eth: status() %08x\n", status);
}

// TODO: I think if this arrives at the wrong time during teardown we
// can deadlock with the ethermac device
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

    mx_status_t status;
    for (;;) {
        if ((status = mx_handle_wait_one(edev->tx_enqueue_fifo, MX_FIFO_NOT_EMPTY, MX_TIME_INFINITE, NULL)) < 0) {
            break;
        }
        if ((status = eth_handle_tx(edev)) < 0) {
            break;
        }
    }

    printf("eth: tx_thread: exit: %d\n", status);
    return 0;
}

static ssize_t eth_get_tx_ioring_locked(ethdev_t* edev, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len) {
    if ((in_len < sizeof(uint32_t)) ||
        (out_len < sizeof(eth_ioring_t))) {
        return ERR_INVALID_ARGS;
    }
    // For now, we can only have one ioring per instance
    if (edev->tx_enqueue_fifo != MX_HANDLE_INVALID) {
        return ERR_ALREADY_BOUND;
    }

    uint32_t entries = *((uint32_t*) in_buf);

    mx_status_t status;
    eth_ioring_t cli, srv;
    if ((status = eth_ioring_create(entries, sizeof(eth_fifo_entry_t), &cli, &srv)) < 0) {
        printf("eth: failed to create tx ioring: %d\n", status);
        return status;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, srv.entries_vmo,
                         0, 2 * entries * sizeof(eth_fifo_entry_t),
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                         (uintptr_t*) &edev->tx_enqueue);
    mx_handle_close(srv.entries_vmo);

    if (status < 0) {
        printf("eth: failed to map tx ioring: %d\n", status);
        mx_handle_close(cli.enqueue_fifo);
        mx_handle_close(cli.dequeue_fifo);
        mx_handle_close(cli.entries_vmo);
        mx_handle_close(srv.enqueue_fifo);
        mx_handle_close(srv.dequeue_fifo);
        return status;
    }

    edev->tx_enqueue_fifo = srv.enqueue_fifo;
    edev->tx_dequeue_fifo = srv.dequeue_fifo;
    edev->tx_size = entries;
    edev->tx_mask = entries - 1;
    edev->tx_dequeue = edev->tx_enqueue + entries;

    memcpy(out_buf, &cli, sizeof(cli));
    return sizeof(cli);
}

static ssize_t eth_get_rx_ioring_locked(ethdev_t* edev, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len) {
    if ((in_len < sizeof(uint32_t)) ||
        (out_len < sizeof(eth_ioring_t))) {
        return ERR_INVALID_ARGS;
    }
    // For now, we can only have one ioring per instance
    if (edev->rx_enqueue_fifo != MX_HANDLE_INVALID) {
        return ERR_ALREADY_BOUND;
    }

    uint32_t entries = *((uint32_t*) in_buf);

    mx_status_t status;
    eth_ioring_t cli, srv;
    if ((status = eth_ioring_create(entries, sizeof(eth_fifo_entry_t), &cli, &srv)) < 0) {
        printf("eth: failed to create tx ioring: %d\n", status);
        return status;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, srv.entries_vmo,
                         0, 2 * entries * sizeof(eth_fifo_entry_t),
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                         (uintptr_t*) &edev->rx_enqueue);
    mx_handle_close(srv.entries_vmo);

    if (status < 0) {
        printf("eth: failed to map rx ioring: %d\n", status);
        mx_handle_close(cli.enqueue_fifo);
        mx_handle_close(cli.dequeue_fifo);
        mx_handle_close(cli.entries_vmo);
        mx_handle_close(srv.enqueue_fifo);
        mx_handle_close(srv.dequeue_fifo);
        return status;
    }

    edev->rx_enqueue_fifo = srv.enqueue_fifo;
    edev->rx_dequeue_fifo = srv.dequeue_fifo;
    edev->rx_size = entries;
    edev->rx_mask = entries - 1;
    edev->rx_dequeue = edev->rx_enqueue + entries;

    memcpy(out_buf, &cli, sizeof(cli));
    return sizeof(cli);
}


static ssize_t eth_set_iobuf_locked(ethdev_t* edev, const void* in_buf, size_t in_len) {
    if (in_len < sizeof(mx_handle_t)) {
        return ERR_INVALID_ARGS;
    }
    if (edev->io_vmo != MX_HANDLE_INVALID) {
        return ERR_ALREADY_BOUND;
    }

    mx_handle_t vmo = *((mx_handle_t*) in_buf);
    size_t size;
    mx_status_t status;

    if ((status = mx_vmo_get_size(vmo, &size)) < 0) {
        printf("eth: could not get io_buf size: %d\n", status);
        mx_handle_close(vmo);
        return status;
    }

    if ((status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                              (uintptr_t*)&edev->io_buf)) < 0) {
        printf("eth: could not map io_buf: %d\n", status);
        mx_handle_close(vmo);
        return status;
    }

    edev->io_vmo = vmo;
    edev->io_size = size;
    return NO_ERROR;
}

static mx_status_t eth_start_locked(ethdev_t* edev) {
    ethdev0_t* edev0 = edev->edev0;

    // Cannot start unless tx/rx rings are configured
    if ((edev->io_vmo == MX_HANDLE_INVALID) ||
        (edev->tx_enqueue_fifo == MX_HANDLE_INVALID) ||
        (edev->rx_enqueue_fifo == MX_HANDLE_INVALID)) {
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
    case IOCTL_ETHERNET_GET_TX_IORING:
        status = eth_get_tx_ioring_locked(edev, in_buf, in_len, out_buf, out_len);
        break;
    case IOCTL_ETHERNET_GET_RX_IORING:
        status = eth_get_rx_ioring_locked(edev, in_buf, in_len, out_buf, out_len);
        break;
    case IOCTL_ETHERNET_SET_IOBUF:
        status = eth_set_iobuf_locked(edev, in_buf, in_len);
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

    printf("eth: kill: tearing down%s\n",
           (edev->state & ETHDEV_TX_THREAD) ? " tx thread" : "");

    // make sure any future ioctls or other ops will fail
    edev->state |= ETHDEV_DEAD;

    // try to convince clients to close us
    if (edev->rx_enqueue_fifo) {
        mx_fifo0_op(edev->rx_enqueue_fifo, MX_FIFO_OP_CONSUMER_EXCEPTION, 1, NULL);
        mx_fifo0_op(edev->rx_dequeue_fifo, MX_FIFO_OP_PRODUCER_EXCEPTION, 1, NULL);
        mx_handle_close(edev->rx_enqueue_fifo);
        mx_handle_close(edev->rx_dequeue_fifo);
        edev->rx_enqueue_fifo = MX_HANDLE_INVALID;
        edev->rx_dequeue_fifo = MX_HANDLE_INVALID;
    }
    if (edev->tx_enqueue_fifo) {
        mx_fifo0_op(edev->tx_enqueue_fifo, MX_FIFO_OP_CONSUMER_EXCEPTION, 1, NULL);
        mx_fifo0_op(edev->tx_dequeue_fifo, MX_FIFO_OP_PRODUCER_EXCEPTION, 1, NULL);
        mx_handle_close(edev->tx_enqueue_fifo);
        mx_handle_close(edev->tx_dequeue_fifo);
        edev->tx_enqueue_fifo = MX_HANDLE_INVALID;
        edev->tx_dequeue_fifo = MX_HANDLE_INVALID;
    }
    if (edev->io_vmo) {
        mx_handle_close(edev->io_vmo);
        edev->io_vmo = MX_HANDLE_INVALID;
    }

    // closing handles will 'encourage' the tx thread to exit
    if (edev->state & ETHDEV_TX_THREAD) {
        edev->state &= (~ETHDEV_TX_THREAD);
        int ret;
        thrd_join(edev->tx_thr, &ret);
        printf("eth: kill: tx thread exited\n");
    }

    if (edev->rx_enqueue) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t) edev->rx_enqueue, 0);
        edev->rx_enqueue = NULL;
        edev->rx_dequeue = NULL;
    }
    if (edev->rx_enqueue) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t) edev->rx_enqueue, 0);
        edev->rx_enqueue = NULL;
        edev->rx_dequeue = NULL;
    }
    if (edev->io_buf) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t) edev->io_buf, 0);
        edev->io_buf = NULL;
    }
    printf("eth: all resources released\n");
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

static mx_status_t eth_bind(mx_driver_t* drv, mx_device_t* dev, void** cookie) {
    ethdev0_t* edev0;
    if ((edev0 = calloc(1, sizeof(ethdev0_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status;
    if (device_get_protocol(dev, MX_PROTOCOL_ETHERMAC, (void**)&edev0->macops)) {
        printf("eth: bind: no ethermac protocol\n");
        status = ERR_INTERNAL;
        goto fail;
    }

    if ((status = edev0->macops->query(dev, 0, &edev0->info)) < 0) {
        printf("eth: bind: ethermac query failed: %d\n", status);
        goto fail;
    }

    if (edev0->info.features & BAD_FEATURES) {
        printf("eth: bind: ethermac requires unsupported features: %08x\n",
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
