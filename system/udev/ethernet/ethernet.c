// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/ethernet.h>

#include <magenta/device/ethernet.h>
#include <magenta/listnode.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define FIFO_DEPTH 256
#define FIFO_ESIZE sizeof(eth_fifo_entry_t)

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do { \
    } while (0)
#endif

// ensure that we will not exceed fifo capacity
static_assert((FIFO_DEPTH * FIFO_ESIZE) <= 4096, "");

// ethernet device
typedef struct ethdev0 {
    // shared state
    mx_device_t* mac;
    ethmac_protocol_t* macops;

    mtx_t lock;

    // active and idle instances (ethdev_t)
    list_node_t list_active;
    list_node_t list_idle;

    ethmac_info_t info;

    mx_device_t* mxdev;
} ethdev0_t;

// transmit thread has been created
#define ETHDEV_TX_THREAD (1u)

// connected to the ethmac and handling traffic
#define ETHDEV_RUNNING (2u)

// being destroyed
#define ETHDEV_DEAD (4u)

// This client should loopback tx packets to rx path
#define ETHDEV_TX_LOOPBACK (8u)

// This client wants to observe loopback tx packets
#define ETHDEV_TX_LISTEN (16u)

// ethernet instance device
typedef struct ethdev {
    list_node_t node;

    ethdev0_t* edev0;

    uint32_t state;

    // fifos are named from the perspective
    // of the packet from from the client
    // to the network interface
    mx_handle_t tx_fifo;
    uint32_t tx_depth;
    mx_handle_t rx_fifo;
    uint32_t rx_depth;

    // io buffer
    mx_handle_t io_vmo;
    void* io_buf;
    size_t io_size;

    // fifo thread
    thrd_t tx_thr;

    mx_device_t* mxdev;

    uint32_t fail_rx_read;
    uint32_t fail_rx_write;
    uint32_t fail_tx_write;
} ethdev_t;

#define FAIL_REPORT_RATE 50

static void eth_handle_rx(ethdev_t* edev, const void* data, size_t len, uint32_t extra) {
    eth_fifo_entry_t e;
    mx_status_t status;
    uint32_t count;

    // TODO: read multiple and cache locally to reduce syscalls
    if ((status = mx_fifo_read(edev->rx_fifo, &e, sizeof(e), &count)) < 0) {
        if (status == ERR_SHOULD_WAIT) {
            if ((edev->fail_rx_read++ % FAIL_REPORT_RATE) == 0) {
                printf("eth: no rx buffers available (%u times)\n",
                       edev->fail_rx_read);
            }
        } else {
            // Fatal, should force teardown
            printf("eth: rx fifo read failed %d\n", status);
        }
        return;
    }

    if ((e.offset >= edev->io_size) || ((e.length > (edev->io_size - e.offset)))) {
        // invalid offset/length. report error. drop packet
        e.length = 0;
        e.flags = ETH_FIFO_INVALID;
    } else if (len > e.length) {
        e.length = 0;
        e.flags = ETH_FIFO_INVALID;
    } else {
        // packet fits. deliver it
        memcpy(edev->io_buf + e.offset, data, len);
        e.length = len;
        e.flags = ETH_FIFO_RX_OK | extra;
    }

    if ((status = mx_fifo_write(edev->rx_fifo, &e, sizeof(e), &count)) < 0) {
        if (status == ERR_SHOULD_WAIT) {
            if ((edev->fail_rx_write++ % FAIL_REPORT_RATE) == 0) {
                printf("eth: no rx_fifo space available (%u times)\n",
                       edev->fail_rx_write);
            }
        } else {
            // Fatal, should force teardown
            printf("eth: rx_fifo write failed %d\n", status);
        }
        return;
    }
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
        eth_handle_rx(edev, data, len, 0);
    }
    mtx_unlock(&edev0->lock);
}

static ethmac_ifc_t ethmac_ifc = {
    .status = eth0_status,
    .recv = eth0_recv,
};

static void eth_tx_echo(ethdev0_t* edev0, const void* data, size_t len) {
    ethdev_t* edev;
    mtx_lock(&edev0->lock);
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        if (edev->state & ETHDEV_TX_LISTEN) {
            eth_handle_rx(edev, data, len, ETH_FIFO_RX_TX);
        }
    }
    mtx_unlock(&edev0->lock);
}

static mx_status_t eth_tx_listen_locked(ethdev_t* edev, bool yes) {
    ethdev0_t* edev0 = edev->edev0;

    // update our state
    if (yes) {
        edev->state |= ETHDEV_TX_LISTEN;
    } else {
        edev->state &= (~ETHDEV_TX_LISTEN);
    }

    // determine global state
    yes = false;
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        if (edev->state & ETHDEV_TX_LISTEN) {
            yes = true;
        }
    }

    // set everyone's echo flag based on global state
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        if (yes) {
            edev->state |= ETHDEV_TX_LOOPBACK;
        } else {
            edev->state &= (~ETHDEV_TX_LOOPBACK);
        }
    }

    return NO_ERROR;
}

static int eth_tx_thread(void* arg) {
    ethdev_t* edev = (ethdev_t*)arg;
    ethdev0_t* edev0 = edev->edev0;
    eth_fifo_entry_t entries[FIFO_DEPTH / 2];
    mx_status_t status;
    uint32_t count;

    for (;;) {
        if ((status = mx_fifo_read(edev->tx_fifo, entries, sizeof(entries), &count)) < 0) {
            if (status == ERR_SHOULD_WAIT) {
                if ((status = mx_object_wait_one(edev->tx_fifo,
                                                 MX_FIFO_READABLE | MX_FIFO_PEER_CLOSED,
                                                 MX_TIME_INFINITE, NULL)) < 0) {
                    if (status != ERR_CANCELED) {
                        printf("eth: tx_fifo: error waiting: %d\n", status);
                    }
                    break;
                }
                continue;
            } else {
                printf("eth: tx_fifo: cannot read: %d\n", status);
                break;
            }
        }

        uint32_t n = count;
        for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
            if ((e->offset > edev->io_size) || ((e->length > (edev->io_size - e->offset)))) {
                e->flags = ETH_FIFO_INVALID;
            } else {
                edev0->macops->send(edev0->mac, 0, edev->io_buf + e->offset, e->length);
                e->flags = ETH_FIFO_TX_OK;
                if (edev->state & ETHDEV_TX_LOOPBACK) {
                    eth_tx_echo(edev0, edev->io_buf + e->offset, e->length);
                }
            }
        }

        if ((status = mx_fifo_write(edev->tx_fifo, entries, sizeof(eth_fifo_entry_t) * n, &count)) < 0) {
            if (status == ERR_SHOULD_WAIT) {
                if ((edev->fail_tx_write++ % FAIL_REPORT_RATE) == 0) {
                    printf("eth: no tx_fifo space available (%u times)\n",
                           edev->fail_tx_write);
                }
            } else {
                printf("eth: tx_fifo write failed %d\n", status);
                break;
            }
        }
        if (count != n) {
            printf("eth: tx_fifo: only wrote %u of %u!\n", count, n);
        }
    }

    printf("eth: tx_thread: exit: %d\n", status);
    return 0;
}

static mx_status_t eth_get_fifos_locked(ethdev_t* edev, void* out_buf, size_t out_len,
                                    size_t* out_actual) {
    if (out_len < sizeof(eth_fifos_t)) {
        return ERR_INVALID_ARGS;
    }
    if (edev->tx_fifo != MX_HANDLE_INVALID) {
        return ERR_ALREADY_BOUND;
    }

    eth_fifos_t* fifos = out_buf;

    mx_status_t status;
    if ((status = mx_fifo_create(FIFO_DEPTH, FIFO_ESIZE, 0, &fifos->tx_fifo, &edev->tx_fifo)) < 0) {
        fprintf(stderr, "eth_create: failed to create tx fifo: %d\n", status);
        return status;
    }
    if ((status = mx_fifo_create(FIFO_DEPTH, FIFO_ESIZE, 0, &fifos->rx_fifo, &edev->rx_fifo)) < 0) {
        fprintf(stderr, "eth_create: failed to create rx fifo: %d\n", status);
        mx_handle_close(fifos->rx_fifo);
        mx_handle_close(edev->tx_fifo);
        edev->tx_fifo = MX_HANDLE_INVALID;
        return status;
    }

    edev->tx_depth = FIFO_DEPTH;
    edev->rx_depth = FIFO_DEPTH;
    fifos->tx_depth = FIFO_DEPTH;
    fifos->rx_depth = FIFO_DEPTH;

    *out_actual = sizeof(*fifos);
    return NO_ERROR;
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
        goto fail;
    }

    if ((status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                              (uintptr_t*)&edev->io_buf)) < 0) {
        printf("eth: could not map io_buf: %d\n", status);
        goto fail;
    }

    edev->io_vmo = vmo;
    edev->io_size = size;

    return NO_ERROR;

fail:
    mx_handle_close(vmo);
    return status;
}

static mx_status_t eth_start_locked(ethdev_t* edev) {
    ethdev0_t* edev0 = edev->edev0;

    // Cannot start unless tx/rx rings are configured
    if ((edev->io_vmo == MX_HANDLE_INVALID) ||
        (edev->tx_fifo == MX_HANDLE_INVALID) ||
        (edev->rx_fifo == MX_HANDLE_INVALID)) {
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

static mx_status_t eth_ioctl(void* ctx, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {

    ethdev_t* edev = ctx;
    mtx_lock(&edev->edev0->lock);
    mx_status_t status;
    if (edev->state & ETHDEV_DEAD) {
        status = ERR_BAD_STATE;
        goto done;
    }

    switch (op) {
    case IOCTL_ETHERNET_GET_INFO: {
        if (out_len < sizeof(eth_info_t)) {
            status = ERR_BUFFER_TOO_SMALL;
        } else {
            eth_info_t* info = out_buf;
            memset(info, 0, sizeof(*info));
            memcpy(info->mac, edev->edev0->info.mac, ETH_MAC_SIZE);
            if (edev->edev0->info.features & ETHMAC_FEATURE_WLAN) {
                info->features |= ETH_FEATURE_WLAN;
            }
            info->mtu = edev->edev0->info.mtu;
            *out_actual = sizeof(*info);
            status = NO_ERROR;
        }
        break;
    }
    case IOCTL_ETHERNET_GET_FIFOS:
        status = eth_get_fifos_locked(edev, out_buf, out_len, out_actual);
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
    case IOCTL_ETHERNET_TX_LISTEN_START:
        status = eth_tx_listen_locked(edev, true);
        break;
    case IOCTL_ETHERNET_TX_LISTEN_STOP:
        status = eth_tx_listen_locked(edev, false);
        break;
    default:
        // TODO: consider if we want this under the edev0->lock or not
        status = device_op_ioctl(edev->edev0->mac, op, in_buf, in_len, out_buf, out_len, out_actual);
        break;
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

    xprintf("eth: kill: tearing down%s\n",
           (edev->state & ETHDEV_TX_THREAD) ? " tx thread" : "");

    // make sure any future ioctls or other ops will fail
    edev->state |= ETHDEV_DEAD;

    // try to convince clients to close us
    if (edev->rx_fifo) {
        mx_handle_close(edev->rx_fifo);
        edev->rx_fifo = MX_HANDLE_INVALID;
    }
    if (edev->tx_fifo) {
        mx_handle_close(edev->tx_fifo);
        edev->tx_fifo = MX_HANDLE_INVALID;
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
        xprintf("eth: kill: tx thread exited\n");
    }

    if (edev->io_buf) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t) edev->io_buf, 0);
        edev->io_buf = NULL;
    }
    xprintf("eth: all resources released\n");
}

static void eth_release(void* ctx) {
    ethdev_t* edev = ctx;
    free(edev);
}

static mx_status_t eth_close(void* ctx, uint32_t flags) {
    ethdev_t* edev = ctx;

    mtx_lock(&edev->edev0->lock);
    eth_stop_locked(edev);
    eth_kill_locked(edev);
    list_delete(&edev->node);
    mtx_unlock(&edev->edev0->lock);

    return NO_ERROR;
}

static mx_protocol_device_t ethdev_ops = {
    .version = DEVICE_OPS_VERSION,
    .close = eth_close,
    .ioctl = eth_ioctl,
    .release = eth_release,
};

static mx_status_t eth0_open(void* ctx, mx_device_t** out, uint32_t flags) {
    ethdev0_t* edev0 = ctx;

    ethdev_t* edev;
    if ((edev = calloc(1, sizeof(ethdev_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    edev->edev0 = edev0;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ethernet",
        .ctx = edev,
        .ops = &ethdev_ops,
        .proto_id = MX_PROTOCOL_ETHERNET,
        .flags = DEVICE_ADD_INSTANCE,
    };

    mx_status_t status;
    if ((status = device_add(edev0->mxdev, &args, &edev->mxdev)) < 0) {
        free(edev);
        return status;
    }

    mtx_lock(&edev0->lock);
    list_add_tail(&edev0->list_idle, &edev->node);
    mtx_unlock(&edev0->lock);

    *out = edev->mxdev;
    return NO_ERROR;
}

static void eth0_unbind(void* ctx) {
    ethdev0_t* edev0 = ctx;

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

    device_remove(edev0->mxdev);
}

static void eth0_release(void* ctx) {
    ethdev0_t* edev0 = ctx;
    free(edev0);
}

static mx_protocol_device_t ethdev0_ops = {
    .version = DEVICE_OPS_VERSION,
    .open = eth0_open,
    .unbind = eth0_unbind,
    .release = eth0_release,
};


#define BAD_FEATURES (ETHMAC_FEATURE_RX_QUEUE | ETHMAC_FEATURE_TX_QUEUE)

static mx_status_t eth_bind(void* ctx, mx_device_t* dev, void** cookie) {
    ethdev0_t* edev0;
    if ((edev0 = calloc(1, sizeof(ethdev0_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status;
    if (device_op_get_protocol(dev, MX_PROTOCOL_ETHERMAC, (void**)&edev0->macops)) {
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

    mtx_init(&edev0->lock, mtx_plain);
    list_initialize(&edev0->list_active);
    list_initialize(&edev0->list_idle);

    edev0->mac = dev;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ethernet",
        .ctx = edev0,
        .ops = &ethdev0_ops,
        .proto_id = MX_PROTOCOL_ETHERNET,
    };

    if ((status = device_add(dev, &args, &edev0->mxdev)) < 0) {
        goto fail;
    }

    return NO_ERROR;

fail:
    free(edev0);
    return status;
}

static mx_driver_ops_t eth_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = eth_bind,
};

MAGENTA_DRIVER_BEGIN(ethernet, eth_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ETHERMAC),
MAGENTA_DRIVER_END(ethernet)
