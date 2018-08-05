// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>

#include <zircon/assert.h>
#include <zircon/device/ethernet.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define FIFO_DEPTH 256
#define FIFO_ESIZE sizeof(eth_fifo_entry_t)
#define DEVICE_NAME_LEN 16

#define PAGE_MASK (PAGE_SIZE - 1)

// This is used for signaling that eth_tx_thread() should exit.
static const zx_signals_t kSignalFifoTerminate = ZX_USER_SIGNAL_0;

// ensure that we will not exceed fifo capacity
static_assert((FIFO_DEPTH * FIFO_ESIZE) <= 4096, "");

// ethernet device
typedef struct ethdev0 {
    // shared state
    zx_device_t* macdev;
    ethmac_protocol_t mac;
    uint32_t state;

    mtx_t lock;

    // active and idle instances (ethdev_t)
    list_node_t list_active;
    list_node_t list_idle;

    int32_t promisc_requesters;
    int32_t multicast_promisc_requesters;

    ethmac_info_t info;
    uint32_t status;
    zx_device_t* zxdev;
} ethdev0_t;

typedef struct tx_info {
    struct ethdev* edev;
    void* fifo_cookie;
    ethmac_netbuf_t netbuf;
} tx_info_t;

// transmit thread has been created
#define ETHDEV_TX_THREAD (1u)

// connected to the ethmac and handling traffic
#define ETHDEV_RUNNING (2u)

// being destroyed
#define ETHDEV_DEAD (4u)

// This client should loopback tx packets to rx path
#define ETHDEV_TX_LOOPBACK (8u)

// This client wants to observe loopback tx packets
#define ETHDEV_TX_LISTEN (0x10u)

// This client has requested promisc mode
#define ETHDEV_PROMISC (0x20u)

// This client has requested multicast promisc mode
#define ETHDEV_MULTICAST_PROMISC (0x40u)

// indicates the device is busy although its lock is released
#define ETHDEV0_BUSY (1u)

// Number of empty fifo entries to read at a time
#define FIFO_BATCH_SZ 32

// How many multicast addresses to remember before punting and turning on multicast-promiscuous
// TODO(eventually): enable deleting addresses
// If this value is changed, change the EthernetMulticastPromiscOnOverflow() test in
//   zircon/system/utest/ethernet/ethernet.cpp
#define MULTICAST_LIST_LIMIT (32)

// ethernet instance device
typedef struct ethdev {
    list_node_t node;

    ethdev0_t* edev0;

    uint32_t state;
    char name[DEVICE_NAME_LEN];

    // fifos are named from the perspective
    // of the packet from from the client
    // to the network interface
    zx_handle_t tx_fifo;
    uint32_t tx_depth;
    zx_handle_t rx_fifo;
    uint32_t rx_depth;
    eth_fifo_entry_t rx_entries[FIFO_BATCH_SZ];
    size_t rx_entry_count;

    // io buffer
    zx_handle_t io_vmo;
    void* io_buf;
    size_t io_size;
    zx_paddr_t* paddr_map;
    zx_handle_t pmt;

    tx_info_t all_tx_bufs[FIFO_DEPTH];
    mtx_t lock;               // Protects free_tx_bufs
    list_node_t free_tx_bufs; // tx_info_t elements

    // fifo thread
    thrd_t tx_thr;

    zx_device_t* zxdev;

    uint8_t multicast[MULTICAST_LIST_LIMIT][ETH_MAC_SIZE];
    uint32_t n_multicast;

    uint32_t fail_rx_read;
    uint32_t fail_rx_write;
    uint32_t fail_tx_write;
} ethdev_t;

#define FAIL_REPORT_RATE 50

static ssize_t eth_promisc_helper_logic_locked(ethdev_t* edev, bool req_on, uint32_t state_bit,
                                               uint32_t param_id, int32_t* requesters_count) {
    if (state_bit == 0 || state_bit & (state_bit - 1)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!req_on == !(edev->state & state_bit)) {
        return ZX_OK; // Duplicate request
    }
    ethdev0_t* edev0 = edev->edev0;
    zx_status_t status = ZX_OK;
    if (req_on) {
        (*requesters_count)++;
        edev->state |= state_bit;
        if (*requesters_count == 1) {
            status = edev0->mac.ops->set_param(edev0->mac.ctx, param_id, true, NULL);
            if (status != ZX_OK) {
                (*requesters_count)--;
                edev->state &= ~state_bit;
            }
        }
    } else {
        (*requesters_count)--;
        edev->state &= ~state_bit;
        if (*requesters_count == 0) {
            status = edev0->mac.ops->set_param(edev0->mac.ctx, param_id, false, NULL);
            if (status != ZX_OK) {
                (*requesters_count)++;
                edev->state |= state_bit;
            }
        }
    }
    return status;
}

static ssize_t eth_set_promisc_locked(ethdev_t* edev, bool req_on) {
    return eth_promisc_helper_logic_locked(edev, req_on, ETHDEV_PROMISC,
                                           ETHMAC_SETPARAM_PROMISC,
                                           &edev->edev0->promisc_requesters);
}

static ssize_t eth_set_multicast_promisc_locked(ethdev_t* edev, bool req_on) {
    return eth_promisc_helper_logic_locked(edev, req_on, ETHDEV_MULTICAST_PROMISC,
                                           ETHMAC_SETPARAM_MULTICAST_PROMISC,
                                           &edev->edev0->multicast_promisc_requesters);
}

static ssize_t eth_rebuild_multicast_filter_locked(ethdev_t* edev) {
    ethdev0_t* edev0 = edev->edev0;
    uint8_t multicast[MULTICAST_LIST_LIMIT][ETH_MAC_SIZE];
    uint32_t n_multicast = 0;
    ethdev_t* edev_i;
    list_for_every_entry(&edev0->list_active, edev_i, ethdev_t, node) {
        for (uint32_t i = 0; i < edev_i->n_multicast; i++) {
            if (n_multicast == MULTICAST_LIST_LIMIT) {
                return edev0->mac.ops->set_param(edev0->mac.ctx, ETHMAC_SETPARAM_MULTICAST_FILTER,
                                                 ETHMAC_MULTICAST_FILTER_OVERFLOW, NULL);
            }
            memcpy(multicast[n_multicast], edev_i->multicast[i], ETH_MAC_SIZE);
            n_multicast++;
        }
    }
    return edev0->mac.ops->set_param(edev0->mac.ctx, ETHMAC_SETPARAM_MULTICAST_FILTER,
                                     n_multicast, multicast);
}

static int eth_multicast_addr_index(ethdev_t* edev, uint8_t* mac) {
    for (uint32_t i = 0; i < edev->n_multicast; i++) {
        if (!memcmp(edev->multicast[i], mac, ETH_MAC_SIZE)) {
            return i;
        }
    }
    return -1;
}

static ssize_t eth_add_multicast_address_locked(ethdev_t* edev, uint8_t* mac) {
    if (!(mac[0] & 1)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (eth_multicast_addr_index(edev, mac) != -1) {
        return ZX_OK;
    }
    if (edev->n_multicast < MULTICAST_LIST_LIMIT) {
        memcpy(edev->multicast[edev->n_multicast], mac, ETH_MAC_SIZE);
        edev->n_multicast++;
        return eth_rebuild_multicast_filter_locked(edev);
    } else {
        ethdev0_t* edev0 = edev->edev0;
        return edev0->mac.ops->set_param(edev0->mac.ctx, ETHMAC_SETPARAM_MULTICAST_FILTER,
                                         ETHMAC_MULTICAST_FILTER_OVERFLOW, NULL);
    }
    return ZX_OK;
}

static ssize_t eth_del_multicast_address_locked(ethdev_t* edev, uint8_t* mac) {
    int ix = eth_multicast_addr_index(edev, mac);
    if (ix == -1) {
        // We may have overflowed the list and not remember an address. Nothing will go wrong if
        // they try to stop listening to an address they never added.
        return ZX_OK;
    }
    edev->n_multicast--;
    memcpy(&edev->multicast[ix], &edev->multicast[edev->n_multicast], ETH_MAC_SIZE);
    return eth_rebuild_multicast_filter_locked(edev);
}

static ssize_t eth_test_clear_multicast_promisc_locked(ethdev_t* edev) {
    zx_status_t status = ZX_OK;
    ethdev_t* edev_i;
    list_for_every_entry(&edev->edev0->list_active, edev_i, ethdev_t, node) {
        if ((status = eth_set_multicast_promisc_locked(edev_i, false)) != ZX_OK) {
            return status;
        }
    }
    return status;
}

static ssize_t eth_config_multicast_locked(ethdev_t* edev, eth_multicast_config_t* config) {
    switch (config->op) {
    case ETH_MULTICAST_ADD_MAC:
        return eth_add_multicast_address_locked(edev, config->mac);
    case ETH_MULTICAST_DEL_MAC:
        return eth_del_multicast_address_locked(edev, config->mac);
    case ETH_MULTICAST_RECV_ALL:
        return eth_set_multicast_promisc_locked(edev, true);
    case ETH_MULTICAST_RECV_FILTER:
        return eth_set_multicast_promisc_locked(edev, false);
    case ETH_MULTICAST_TEST_FILTER:
        zxlogf(INFO,
               "MULTICAST_TEST_FILTER invoked. Turning multicast-promisc off unconditionally.\n");
        return eth_test_clear_multicast_promisc_locked(edev);
    case ETH_MULTICAST_DUMP_REGS:
        return edev->edev0->mac.ops->set_param(edev->edev0->mac.ctx,
                                               ETHMAC_SETPARAM_DUMP_REGS, 0, NULL);
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static void eth_handle_rx(ethdev_t* edev, const void* data, size_t len, uint32_t extra) {
    zx_status_t status;
    size_t count;

    if (edev->rx_entry_count == 0) {
        status = zx_fifo_read(edev->rx_fifo, sizeof(edev->rx_entries[0]), edev->rx_entries,
                              countof(edev->rx_entries), &count);
        if (status != ZX_OK) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                if ((edev->fail_rx_read++ % FAIL_REPORT_RATE) == 0) {
                    zxlogf(ERROR, "eth [%s]: no rx buffers available (%u times)\n",
                           edev->name, edev->fail_rx_read);
                }
            } else {
                // Fatal, should force teardown
                zxlogf(ERROR, "eth [%s]: rx fifo read failed %d\n", edev->name, status);
            }
            return;
        }
        edev->rx_entry_count = count;
    }

    eth_fifo_entry_t* e = &edev->rx_entries[--edev->rx_entry_count];
    if ((e->offset >= edev->io_size) || ((e->length > (edev->io_size - e->offset)))) {
        // invalid offset/length. report error. drop packet
        e->length = 0;
        e->flags = ETH_FIFO_INVALID;
    } else if (len > e->length) {
        e->length = 0;
        e->flags = ETH_FIFO_INVALID;
    } else {
        // packet fits. deliver it
        memcpy(edev->io_buf + e->offset, data, len);
        e->length = len;
        e->flags = ETH_FIFO_RX_OK | extra;
    }

    if ((status = zx_fifo_write(edev->rx_fifo, sizeof(*e), e, 1, NULL)) < 0) {
        if (status == ZX_ERR_SHOULD_WAIT) {
            if ((edev->fail_rx_write++ % FAIL_REPORT_RATE) == 0) {
                zxlogf(ERROR, "eth [%s]: no rx_fifo space available (%u times)\n",
                       edev->name, edev->fail_rx_write);
            }
        } else {
            // Fatal, should force teardown
            zxlogf(ERROR, "eth [%s]: rx_fifo write failed %d\n", edev->name, status);
        }
        return;
    }
}

static void eth0_status(void* cookie, uint32_t status) {
    zxlogf(TRACE, "eth: status() %08x\n", status);

    ethdev0_t* edev0 = cookie;
    mtx_lock(&edev0->lock);
    edev0->status = status;

    ethdev_t* edev;
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        zx_object_signal_peer(edev->rx_fifo, 0, ETH_SIGNAL_STATUS);
    }
    mtx_unlock(&edev0->lock);
}

static int tx_fifo_write(ethdev_t* edev, eth_fifo_entry_t* entries, size_t count) {
    zx_status_t status;
    size_t actual;
    // Writing should never fail, or fail to write all entries
    status = zx_fifo_write(edev->tx_fifo, sizeof(eth_fifo_entry_t), entries, count, &actual);
    if (status < 0) {
        zxlogf(ERROR, "eth [%s]: tx_fifo write failed %d\n", edev->name, status);
        return -1;
    }
    if (actual != count) {
        zxlogf(ERROR, "eth [%s]: tx_fifo: only wrote %zu of %zu!\n", edev->name, actual, count);
        return -1;
    }
    return 0;
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

// Borrows a TX buffer from the pool. Logs and returns NULL if none is available
static tx_info_t* eth_get_tx_info(ethdev_t* edev) {
    mtx_lock(&edev->lock);
    tx_info_t* tx_info = list_remove_head_type(&edev->free_tx_bufs, tx_info_t, netbuf.node);
    mtx_unlock(&edev->lock);
    if (tx_info == NULL) {
        zxlogf(ERROR, "eth [%s]: tx_info pool empty\n", edev->name);
    }
    return tx_info;
}

// Returns a TX buffer to the pool
static void eth_put_tx_info(ethdev_t* edev, tx_info_t* tx_info) {
    mtx_lock(&edev->lock);
    list_add_head(&edev->free_tx_bufs, &tx_info->netbuf.node);
    mtx_unlock(&edev->lock);
}

static void eth0_complete_tx(void* cookie, ethmac_netbuf_t* netbuf, zx_status_t status) {
    tx_info_t* tx_info = containerof(netbuf, tx_info_t, netbuf);
    ethdev_t* edev = tx_info->edev;
    eth_fifo_entry_t entry = {.offset = netbuf->data - edev->io_buf,
                              .length = netbuf->len,
                              .flags = status == ZX_OK ? ETH_FIFO_TX_OK : 0,
                              .cookie = tx_info->fifo_cookie};

    // Now that we've copied all pertinent data from the netbuf, return it to the free list so
    // it is avaialble immediately for the next request.
    eth_put_tx_info(edev, tx_info);

    // Send the eth_fifo_entry back to the client
    tx_fifo_write(edev, &entry, 1);
}

static ethmac_ifc_t ethmac_ifc = {
    .status = eth0_status,
    .recv = eth0_recv,
    .complete_tx = eth0_complete_tx,
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

static zx_status_t eth_tx_listen_locked(ethdev_t* edev, bool yes) {
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

    return ZX_OK;
}

// The array of entries is invalidated after the call
static int eth_send(ethdev_t* edev, eth_fifo_entry_t* entries, uint32_t count) {
    tx_info_t* tx_info = NULL;
    ethdev0_t* edev0 = edev->edev0;
    // The entries that we can't send back to the fifo immediately are filtered
    // out in-place using a classic algorithm a-la "std::remove_if".
    // Once the loop finishes, the first 'to_write' entries in the array
    // will be written back to the fifo. The rest will be written later by
    // the eth0_complete_tx callback.
    uint32_t to_write = 0;
    for (eth_fifo_entry_t* e = entries; count > 0; e++) {
        if ((e->offset > edev->io_size) || ((e->length > (edev->io_size - e->offset)))) {
            e->flags = ETH_FIFO_INVALID;
            entries[to_write++] = *e;
        } else {
            zx_status_t status;
            if (tx_info == NULL) {
                tx_info = eth_get_tx_info(edev);
                if (tx_info == NULL) {
                    return -1;
                }
            }
            uint32_t opts = count > 1 ? ETHMAC_TX_OPT_MORE : 0u;
            if (opts) {
                zxlogf(SPEW, "setting OPT_MORE (%u packets to go)\n", count);
            }
            tx_info->netbuf.data = edev->io_buf + e->offset;
            if (edev0->info.features & ETHMAC_FEATURE_DMA) {
                tx_info->netbuf.phys = edev->paddr_map[e->offset / PAGE_SIZE] +
                                       (e->offset & PAGE_MASK);
            }
            tx_info->netbuf.len = e->length;
            tx_info->fifo_cookie = e->cookie;
            status = edev0->mac.ops->queue_tx(edev0->mac.ctx, opts, &tx_info->netbuf);
            if (edev->state & ETHDEV_TX_LOOPBACK) {
                eth_tx_echo(edev0, edev->io_buf + e->offset, e->length);
            }
            if (status != ZX_ERR_SHOULD_WAIT) {
                // Transmission completed. To avoid extra mutex locking/unlocking,
                // we don't return the buffer to the pool immediately, but reuse
                // it on the next iteration of the loop.
                e->flags = status == ZX_OK ? ETH_FIFO_TX_OK : 0;
                entries[to_write++] = *e;
            } else {
                // The ownership of the TX buffer is transferred to mac.ops->queue_tx().
                // We can't reuse it, so clear the pointer.
                tx_info = NULL;
            }
        }
        count--;
    }
    if (tx_info) {
        eth_put_tx_info(edev, tx_info);
    }
    if (to_write) {
        tx_fifo_write(edev, entries, to_write);
    }
    return 0;
}

static int eth_tx_thread(void* arg) {
    ethdev_t* edev = (ethdev_t*)arg;
    eth_fifo_entry_t entries[FIFO_DEPTH / 2];
    zx_status_t status;
    size_t count;

    for (;;) {
        if ((status = zx_fifo_read(edev->tx_fifo, sizeof(entries[0]), entries,
                                   countof(entries), &count)) < 0) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                zx_signals_t observed;
                if ((status = zx_object_wait_one(edev->tx_fifo,
                                                 ZX_FIFO_READABLE |
                                                 ZX_FIFO_PEER_CLOSED |
                                                 kSignalFifoTerminate,
                                                 ZX_TIME_INFINITE,
                                                 &observed)) < 0) {
                    zxlogf(ERROR, "eth [%s]: tx_fifo: error waiting: %d\n", edev->name, status);
                    break;
                }
                if (observed & kSignalFifoTerminate)
                    break;
                continue;
            } else {
                zxlogf(ERROR, "eth [%s]: tx_fifo: cannot read: %d\n", edev->name, status);
                break;
            }
        }
        if (eth_send(edev, entries, count)) {
            break;
        }
    }

    zxlogf(INFO, "eth [%s]: tx_thread: exit: %d\n", edev->name, status);
    return 0;
}

static zx_status_t eth_get_fifos_locked(ethdev_t* edev, void* out_buf, size_t out_len,
                                        size_t* out_actual) {
    if (out_len < sizeof(eth_fifos_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (edev->tx_fifo != ZX_HANDLE_INVALID) {
        return ZX_ERR_ALREADY_BOUND;
    }

    eth_fifos_t* fifos = out_buf;

    zx_status_t status;
    if ((status = zx_fifo_create(FIFO_DEPTH, FIFO_ESIZE, 0, &fifos->tx_fifo, &edev->tx_fifo)) < 0) {
        zxlogf(ERROR, "eth_create  [%s]: failed to create tx fifo: %d\n", edev->name, status);
        return status;
    }
    if ((status = zx_fifo_create(FIFO_DEPTH, FIFO_ESIZE, 0, &fifos->rx_fifo, &edev->rx_fifo)) < 0) {
        zxlogf(ERROR, "eth_create  [%s]: failed to create rx fifo: %d\n", edev->name, status);
        zx_handle_close(fifos->tx_fifo);
        zx_handle_close(edev->tx_fifo);
        edev->tx_fifo = ZX_HANDLE_INVALID;
        return status;
    }

    edev->tx_depth = FIFO_DEPTH;
    edev->rx_depth = FIFO_DEPTH;
    fifos->tx_depth = FIFO_DEPTH;
    fifos->rx_depth = FIFO_DEPTH;

    *out_actual = sizeof(*fifos);
    return ZX_OK;
}

static ssize_t eth_set_iobuf_locked(ethdev_t* edev, const void* in_buf, size_t in_len) {
    if (in_len < sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (edev->io_vmo != ZX_HANDLE_INVALID || edev->io_buf != NULL) {
        return ZX_ERR_ALREADY_BOUND;
    }

    zx_handle_t vmo = *((zx_handle_t*)in_buf);

    size_t size;
    zx_status_t status;

    if ((status = zx_vmo_get_size(vmo, &size)) < 0) {
        zxlogf(ERROR, "eth [%s]: could not get io_buf size: %d\n", edev->name, status);
        goto fail;
    }

    if ((status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                              ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                              ZX_VM_FLAG_REQUIRE_NON_RESIZABLE,
                              (uintptr_t*)&edev->io_buf)) < 0) {
        zxlogf(ERROR, "eth [%s]: could not map io_buf: %d\n", edev->name, status);
        goto fail;
    }

    // If the driver indicates that it will be doing DMA to/from the vmo,
    // we pin the memory and cache the physical address list
    if (edev->edev0->info.features & ETHMAC_FEATURE_DMA) {
        size_t pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
        edev->paddr_map = malloc(pages * sizeof(zx_paddr_t));
        if (!edev->paddr_map) {
            status = ZX_ERR_NO_MEMORY;
            goto fail;
        }
        zx_handle_t bti = edev->edev0->mac.ops->get_bti(edev->edev0->mac.ctx);
        if ((status = zx_bti_pin(bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
                                 vmo, 0, size, edev->paddr_map, pages, &edev->pmt)) != ZX_OK) {
            zxlogf(ERROR, "eth [%s]: bti_pin failed, can't pin vmo: %d\n",
                   edev->name, status);
            goto fail;
        }
    }
    edev->io_vmo = vmo;
    edev->io_size = size;

    return ZX_OK;

fail:
    if (edev->io_buf) {
        zx_status_t unmap_status =
            zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)edev->io_buf, size);
        if (unmap_status != ZX_OK) {
            zxlogf(ERROR, "eth [%s]: could not unmap io_buf: %d\n",
                   edev->name, unmap_status);
            status = unmap_status;
        }
        edev->io_buf = NULL;
    }
    free(edev->paddr_map);
    edev->paddr_map = NULL;
    zx_handle_close(vmo);
    return status;
}

// The thread safety analysis cannot reason through the aliasing of
// edev0 and edev->edev0, so disable it.
static zx_status_t eth_start_locked(ethdev_t* edev) TA_NO_THREAD_SAFETY_ANALYSIS {
    ethdev0_t* edev0 = edev->edev0;

    // Cannot start unless tx/rx rings are configured
    if ((edev->io_vmo == ZX_HANDLE_INVALID) ||
        (edev->tx_fifo == ZX_HANDLE_INVALID) ||
        (edev->rx_fifo == ZX_HANDLE_INVALID)) {
        return ZX_ERR_BAD_STATE;
    }

    if (edev->state & ETHDEV_RUNNING) {
        return ZX_OK;
    }

    if (!(edev->state & ETHDEV_TX_THREAD)) {
        int r = thrd_create_with_name(&edev->tx_thr, eth_tx_thread,
                                      edev, "eth-tx-thread");
        if (r != thrd_success) {
            zxlogf(ERROR, "eth [%s]: failed to start tx thread: %d\n", edev->name, r);
            return ZX_ERR_INTERNAL;
        }
        edev->state |= ETHDEV_TX_THREAD;
    }

    zx_status_t status;
    if (list_is_empty(&edev0->list_active)) {
        // Release the lock to allow other device operations in callback routine.
        // Re-acquire lock afterwards. Set busy to prevent problems with other ioctls.
        edev0->state |= ETHDEV0_BUSY;
        mtx_unlock(&edev0->lock);
        status = edev0->mac.ops->start(edev0->mac.ctx, &ethmac_ifc, edev0);
        mtx_lock(&edev0->lock);
        edev0->state &= ~ETHDEV0_BUSY;
    } else {
        status = ZX_OK;
    }

    if (status == ZX_OK) {
        edev->state |= ETHDEV_RUNNING;
        list_delete(&edev->node);
        list_add_tail(&edev0->list_active, &edev->node);
        // TODO - After we get IGMP, don't automatically set multicast promisc true
        eth_set_multicast_promisc_locked(edev, true);
        // Trigger the status signal so the client will query the status at the start.
        zx_object_signal_peer(edev->rx_fifo, 0, ETH_SIGNAL_STATUS);
    } else {
        zxlogf(ERROR, "eth [%s]: failed to start mac: %d\n", edev->name, status);
    }

    return status;
}

// The thread safety analysis cannot reason through the aliasing of
// edev0 and edev->edev0, so disable it.
static zx_status_t eth_stop_locked(ethdev_t* edev) TA_NO_THREAD_SAFETY_ANALYSIS {
    ethdev0_t* edev0 = edev->edev0;

    if (edev->state & ETHDEV_RUNNING) {
        edev->state &= (~ETHDEV_RUNNING);
        list_delete(&edev->node);
        list_add_tail(&edev0->list_idle, &edev->node);
        // The next three lines clean up promisc, multicast-promisc, and multicast-filter, in case
        // this ethdev had any state set. Ignore failures, which may come from drivers not
        // supporting the feature. (TODO: check failure codes).
        eth_set_promisc_locked(edev, false);
        eth_set_multicast_promisc_locked(edev, false);
        eth_rebuild_multicast_filter_locked(edev);
        if (list_is_empty(&edev0->list_active)) {
            if (!(edev->state & ETHDEV_DEAD)) {
                // Release the lock to allow other device operations in callback routine.
                // Re-acquire lock afterwards. Set busy to prevent problems with other ioctls.
                edev0->state |= ETHDEV0_BUSY;
                mtx_unlock(&edev0->lock);
                edev0->mac.ops->stop(edev0->mac.ctx);
                mtx_lock(&edev0->lock);
                edev0->state &= ~ETHDEV0_BUSY;
            }
        }
    }

    return ZX_OK;
}

static ssize_t eth_set_client_name_locked(ethdev_t* edev, const void* in_buf, size_t in_len) {
    if (in_len >= DEVICE_NAME_LEN) {
        in_len = DEVICE_NAME_LEN - 1;
    }
    memcpy(edev->name, in_buf, in_len);
    edev->name[in_len] = '\0';
    return ZX_OK;
}

static zx_status_t eth_get_status_locked(ethdev_t* edev, void* out_buf, size_t out_len,
                                         size_t* out_actual) {
    if (out_len < sizeof(uint32_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (edev->rx_fifo == ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_STATE;
    }
    if (zx_object_signal_peer(edev->rx_fifo, ETH_SIGNAL_STATUS, 0) != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }

    uint32_t* status = out_buf;
    *status = edev->edev0->status;
    *out_actual = sizeof(*status);
    return ZX_OK;
}

static zx_status_t eth_ioctl(void* ctx, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {

    ethdev_t* edev = ctx;
    mtx_lock(&edev->edev0->lock);
    zx_status_t status;
    if (edev->edev0->state & ETHDEV0_BUSY) {
        zxlogf(ERROR, "eth [%s]: cannot perform ioctl while device is busy. ioctl: %u\n",
               edev->name, IOCTL_NUMBER(op));
        status = ZX_ERR_SHOULD_WAIT;
        goto done;
    }

    if (edev->state & ETHDEV_DEAD) {
        status = ZX_ERR_BAD_STATE;
        goto done;
    }

    switch (op) {
    case IOCTL_ETHERNET_GET_INFO: {
        if (out_len < sizeof(eth_info_t)) {
            status = ZX_ERR_BUFFER_TOO_SMALL;
        } else {
            eth_info_t* info = out_buf;
            memset(info, 0, sizeof(*info));
            memcpy(info->mac, edev->edev0->info.mac, ETH_MAC_SIZE);
            if (edev->edev0->info.features & ETHMAC_FEATURE_WLAN) {
                info->features |= ETH_FEATURE_WLAN;
            }
            if (edev->edev0->info.features & ETHMAC_FEATURE_SYNTH) {
                info->features |= ETH_FEATURE_SYNTH;
            }
            info->mtu = edev->edev0->info.mtu;
            *out_actual = sizeof(*info);
            status = ZX_OK;
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
    case IOCTL_ETHERNET_SET_CLIENT_NAME:
        status = eth_set_client_name_locked(edev, in_buf, in_len);
        break;
    case IOCTL_ETHERNET_GET_STATUS:
        status = eth_get_status_locked(edev, out_buf, out_len, out_actual);
        break;
    case IOCTL_ETHERNET_SET_PROMISC:
        if (in_len != sizeof(bool) || in_buf == NULL) {
            status = ZX_ERR_INVALID_ARGS;
            goto done;
        }
        status = eth_set_promisc_locked(edev, *(bool*)in_buf);
        break;
    case IOCTL_ETHERNET_CONFIG_MULTICAST:
        if (in_len != sizeof(eth_multicast_config_t) || in_buf == NULL) {
            status = ZX_ERR_INVALID_ARGS;
            goto done;
        }
        status = eth_config_multicast_locked(edev, (eth_multicast_config_t*)in_buf);
        break;
    default:
        // TODO: consider if we want this under the edev0->lock or not
        status = device_ioctl(edev->edev0->macdev, op, in_buf, in_len, out_buf, out_len, out_actual);
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

    zxlogf(TRACE, "eth [%s]: kill: tearing down%s\n",
           edev->name, (edev->state & ETHDEV_TX_THREAD) ? " tx thread" : "");
    eth_set_promisc_locked(edev, false);

    // make sure any future ioctls or other ops will fail
    edev->state |= ETHDEV_DEAD;

    // try to convince clients to close us
    if (edev->rx_fifo) {
        zx_handle_close(edev->rx_fifo);
        edev->rx_fifo = ZX_HANDLE_INVALID;
    }
    if (edev->tx_fifo) {
        // Ask the TX thread to exit.
        zx_object_signal(edev->tx_fifo, 0, kSignalFifoTerminate);
    }
    if (edev->io_vmo) {
        zx_handle_close(edev->io_vmo);
        edev->io_vmo = ZX_HANDLE_INVALID;
    }

    if (edev->state & ETHDEV_TX_THREAD) {
        edev->state &= (~ETHDEV_TX_THREAD);
        int ret;
        thrd_join(edev->tx_thr, &ret);
        zxlogf(TRACE, "eth [%s]: kill: tx thread exited\n", edev->name);
    }

    if (edev->tx_fifo) {
        zx_handle_close(edev->tx_fifo);
        edev->tx_fifo = ZX_HANDLE_INVALID;
    }

    if (edev->io_buf) {
        zx_status_t status =
            zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)edev->io_buf, edev->io_size);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        edev->io_buf = NULL;
    }
    if (edev->paddr_map != NULL) {
        if (zx_pmt_unpin(edev->pmt) != ZX_OK) {
            zxlogf(ERROR, "eth [%s]: cannot unpin vmo?!\n", edev->name);
        }
        free(edev->paddr_map);
        edev->paddr_map = NULL;
        edev->pmt = ZX_HANDLE_INVALID;
    }
    zxlogf(TRACE, "eth [%s]: all resources released\n", edev->name);
}

static void eth_release(void* ctx) {
    ethdev_t* edev = ctx;
    if (edev) {
        free(edev->paddr_map);
    }
    free(edev);
}

static zx_status_t eth_close(void* ctx, uint32_t flags) {
    ethdev_t* edev = ctx;

    mtx_lock(&edev->edev0->lock);
    eth_stop_locked(edev);
    eth_kill_locked(edev);
    list_delete(&edev->node);
    mtx_unlock(&edev->edev0->lock);

    return ZX_OK;
}

static zx_protocol_device_t ethdev_ops = {
    .version = DEVICE_OPS_VERSION,
    .close = eth_close,
    .ioctl = eth_ioctl,
    .release = eth_release,
};

static zx_status_t eth0_open(void* ctx, zx_device_t** out, uint32_t flags) {
    ethdev0_t* edev0 = ctx;

    ethdev_t* edev;
    if ((edev = calloc(1, sizeof(ethdev_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    edev->edev0 = edev0;

    list_initialize(&edev->free_tx_bufs);
    for (size_t ndx = 0; ndx < FIFO_DEPTH; ndx++) {
        edev->all_tx_bufs[ndx].edev = edev;
        list_add_tail(&edev->free_tx_bufs, &edev->all_tx_bufs[ndx].netbuf.node);
    }
    mtx_init(&edev->lock, mtx_plain);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ethernet",
        .ctx = edev,
        .ops = &ethdev_ops,
        .proto_id = ZX_PROTOCOL_ETHERNET,
        .flags = DEVICE_ADD_INSTANCE,
    };

    zx_status_t status;
    if ((status = device_add(edev0->zxdev, &args, &edev->zxdev)) < 0) {
        free(edev);
        return status;
    }

    mtx_lock(&edev0->lock);
    list_add_tail(&edev0->list_idle, &edev->node);
    mtx_unlock(&edev0->lock);

    *out = edev->zxdev;
    return ZX_OK;
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

    device_remove(edev0->zxdev);
}

static void eth0_release(void* ctx) {
    ethdev0_t* edev0 = ctx;
    free(edev0);
}

static zx_protocol_device_t ethdev0_ops = {
    .version = DEVICE_OPS_VERSION,
    .open = eth0_open,
    .unbind = eth0_unbind,
    .release = eth0_release,
};

static zx_status_t eth_bind(void* ctx, zx_device_t* dev) {
    ethdev0_t* edev0;
    if ((edev0 = calloc(1, sizeof(ethdev0_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if (device_get_protocol(dev, ZX_PROTOCOL_ETHERNET_IMPL, &edev0->mac)) {
        zxlogf(ERROR, "eth: bind: no ethermac protocol\n");
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    ethmac_protocol_ops_t* ops = edev0->mac.ops;
    if (ops->query == NULL || ops->stop == NULL || ops->start == NULL || ops->queue_tx == NULL ||
        ops->set_param == NULL) {
        zxlogf(ERROR, "eth: bind: device '%s': incomplete ethermac protocol\n",
               device_get_name(dev));
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    if ((status = edev0->mac.ops->query(edev0->mac.ctx, 0, &edev0->info)) < 0) {
        zxlogf(ERROR, "eth: bind: ethermac query failed: %d\n", status);
        goto fail;
    }

    if ((edev0->info.features & ETHMAC_FEATURE_DMA) &&
        (ops->get_bti == NULL)) {
        zxlogf(ERROR, "eth: bind: device '%s': does not implement ops->get_bti()\n",
               device_get_name(dev));
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    mtx_init(&edev0->lock, mtx_plain);
    list_initialize(&edev0->list_active);
    list_initialize(&edev0->list_idle);

    edev0->macdev = dev;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ethernet",
        .ctx = edev0,
        .ops = &ethdev0_ops,
        .proto_id = ZX_PROTOCOL_ETHERNET,
    };

    if ((status = device_add(dev, &args, &edev0->zxdev)) < 0) {
        goto fail;
    }

    return ZX_OK;

fail:
    free(edev0);
    return status;
}

static zx_driver_ops_t eth_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = eth_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(ethernet, eth_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ETHERNET_IMPL),
ZIRCON_DRIVER_END(ethernet)
// clang-format on
