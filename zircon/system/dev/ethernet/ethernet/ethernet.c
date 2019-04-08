// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>

#include <fuchsia/hardware/ethernet/c/fidl.h>

#include <zircon/assert.h>
#include <zircon/device/ethernet.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/status.h>
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

    uint64_t open_count;
    uint32_t state;
    char name[fuchsia_hardware_ethernet_MAX_CLIENT_NAME_LEN + 1];

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

    // FIFO_DEPTH entries, each |tx_size| large.
    void *all_tx_bufs;
    size_t tx_size;

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

typedef struct tx_info {
    struct ethdev* edev;
    uint64_t fifo_cookie;
    list_node_t node;
} tx_info_t;

static tx_info_t* netbuf_to_tx_info(ethdev0_t* edev0, ethmac_netbuf_t* netbuf) {
    return (tx_info_t*)((uintptr_t)netbuf + edev0->info.netbuf_size);
}

static ethmac_netbuf_t* tx_info_to_netbuf(ethdev0_t* edev0, tx_info_t* tx_info) {
    return (ethmac_netbuf_t*)((uintptr_t)tx_info - edev0->info.netbuf_size);
}

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
            status = ethmac_set_param(&edev0->mac, param_id, true, NULL, 0);
            if (status != ZX_OK) {
                (*requesters_count)--;
                edev->state &= ~state_bit;
            }
        }
    } else {
        (*requesters_count)--;
        edev->state &= ~state_bit;
        if (*requesters_count == 0) {
            status = ethmac_set_param(&edev0->mac, param_id, false, NULL, 0);
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
                return ethmac_set_param(&edev0->mac, ETHMAC_SETPARAM_MULTICAST_FILTER,
                                        ETHMAC_MULTICAST_FILTER_OVERFLOW, NULL, 0);
            }
            memcpy(multicast[n_multicast], edev_i->multicast[i], ETH_MAC_SIZE);
            n_multicast++;
        }
    }
    return ethmac_set_param(&edev0->mac, ETHMAC_SETPARAM_MULTICAST_FILTER, n_multicast, multicast,
                            n_multicast * ETH_MAC_SIZE);
}

static int eth_multicast_addr_index(ethdev_t* edev, const uint8_t* mac) {
    for (uint32_t i = 0; i < edev->n_multicast; i++) {
        if (!memcmp(edev->multicast[i], mac, ETH_MAC_SIZE)) {
            return i;
        }
    }
    return -1;
}

static ssize_t eth_add_multicast_address_locked(ethdev_t* edev, const uint8_t* mac) {
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
        return ethmac_set_param(&edev0->mac, ETHMAC_SETPARAM_MULTICAST_FILTER,
                                ETHMAC_MULTICAST_FILTER_OVERFLOW, NULL, 0);
    }
    return ZX_OK;
}

static ssize_t eth_del_multicast_address_locked(ethdev_t* edev, const uint8_t* mac) {
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

static void eth_handle_rx(ethdev_t* edev, const void* data, size_t len, uint32_t extra) {
    zx_status_t status;
    size_t count;

    if (edev->rx_entry_count == 0) {
        status = zx_fifo_read(edev->rx_fifo, sizeof(edev->rx_entries[0]), edev->rx_entries,
                              countof(edev->rx_entries), &count);
        if (status != ZX_OK) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                edev->fail_rx_read += 1;
                if (edev->fail_rx_read == 1 ||
                        (edev->fail_rx_read % FAIL_REPORT_RATE) == 0) {
                    zxlogf(WARN, "eth [%s]: warning: no rx buffers available, frame dropped "
                            "(%u time%s)\n",
                           edev->name, edev->fail_rx_read, edev->fail_rx_read > 1 ? "s" : "");
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

    static_assert(ETHMAC_STATUS_ONLINE == fuchsia_hardware_ethernet_DEVICE_STATUS_ONLINE, "");
    edev0->status = status;

    static_assert(fuchsia_hardware_ethernet_SIGNAL_STATUS == ZX_USER_SIGNAL_0, "");
    ethdev_t* edev;
    list_for_every_entry(&edev0->list_active, edev, ethdev_t, node) {
        zx_object_signal_peer(edev->rx_fifo, 0, fuchsia_hardware_ethernet_SIGNAL_STATUS);
    }
    mtx_unlock(&edev0->lock);
}

static int tx_fifo_write(ethdev_t* edev, eth_fifo_entry_t* entries,
                         size_t count) {
    zx_status_t status;
    size_t actual;
    // Writing should never fail, or fail to write all entries
    status = zx_fifo_write(edev->tx_fifo, sizeof(eth_fifo_entry_t), entries,
                           count, &actual);
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
static void eth0_recv(void* cookie, const void* data, size_t len, uint32_t flags) {
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
    tx_info_t* tx_info = list_remove_head_type(&edev->free_tx_bufs, tx_info_t, node);
    mtx_unlock(&edev->lock);
    if (tx_info == NULL) {
        zxlogf(ERROR, "eth [%s]: tx_info pool empty\n", edev->name);
    }
    return tx_info;
}

// Returns a TX buffer to the pool
static void eth_put_tx_info(ethdev_t* edev, tx_info_t* tx_info) {
    mtx_lock(&edev->lock);
    list_add_head(&edev->free_tx_bufs, &tx_info->node);
    mtx_unlock(&edev->lock);
}

static void eth0_complete_tx(void* cookie, ethmac_netbuf_t* netbuf, zx_status_t status) {
    ethdev0_t* edev0 = cookie;
    tx_info_t* tx_info = netbuf_to_tx_info(edev0, netbuf);
    ethdev_t* edev = tx_info->edev;
    eth_fifo_entry_t entry = {
        .offset = netbuf->data_buffer - edev->io_buf,
        .length = netbuf->data_size,
        .flags = status == ZX_OK ? ETH_FIFO_TX_OK : 0,
        .cookie = tx_info->fifo_cookie};

    // Now that we've copied all pertinent data from the netbuf, return it to the free list so
    // it is available immediately for the next request.
    eth_put_tx_info(edev, tx_info);

    // Send the entry back to the client
    tx_fifo_write(edev, &entry, 1);
}

static ethmac_ifc_protocol_ops_t ethmac_ifc = {
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
            ethmac_netbuf_t* netbuf = tx_info_to_netbuf(edev0, tx_info);
            netbuf->data_buffer = edev->io_buf + e->offset;
            if (edev0->info.features & ETHMAC_FEATURE_DMA) {
                netbuf->phys = edev->paddr_map[e->offset / PAGE_SIZE] +
                                       (e->offset & PAGE_MASK);
            }
            netbuf->data_size = e->length;
            tx_info->fifo_cookie = e->cookie;
            status = ethmac_queue_tx(&edev0->mac, opts, netbuf);
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

static zx_status_t eth_get_fifos_locked(ethdev_t* edev,
                                        struct fuchsia_hardware_ethernet_Fifos* fifos) {
    zx_status_t status;
    if ((status = zx_fifo_create(FIFO_DEPTH, FIFO_ESIZE, 0, &fifos->tx, &edev->tx_fifo)) < 0) {
        zxlogf(ERROR, "eth_create  [%s]: failed to create tx fifo: %d\n", edev->name, status);
        return status;
    }
    if ((status = zx_fifo_create(FIFO_DEPTH, FIFO_ESIZE, 0, &fifos->rx, &edev->rx_fifo)) < 0) {
        zxlogf(ERROR, "eth_create  [%s]: failed to create rx fifo: %d\n", edev->name, status);
        zx_handle_close(fifos->tx);
        zx_handle_close(edev->tx_fifo);
        edev->tx_fifo = ZX_HANDLE_INVALID;
        return status;
    }

    edev->tx_depth = FIFO_DEPTH;
    edev->rx_depth = FIFO_DEPTH;
    fifos->tx_depth = FIFO_DEPTH;
    fifos->rx_depth = FIFO_DEPTH;

    return ZX_OK;
}

static ssize_t eth_set_iobuf_locked(ethdev_t* edev, zx_handle_t vmo) {
    if (edev->io_vmo != ZX_HANDLE_INVALID || edev->io_buf != NULL) {
        return ZX_ERR_ALREADY_BOUND;
    }

    size_t size;
    zx_status_t status;

    if ((status = zx_vmo_get_size(vmo, &size)) < 0) {
        zxlogf(ERROR, "eth [%s]: could not get io_buf size: %d\n", edev->name, status);
        goto fail;
    }

    if ((status = zx_vmar_map(zx_vmar_root_self(),
                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE |
                              ZX_VM_REQUIRE_NON_RESIZABLE,
                              0, vmo, 0, size, (uintptr_t*)&edev->io_buf)) < 0) {
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
        zx_handle_t bti = ZX_HANDLE_INVALID;
        ethmac_get_bti(&edev->edev0->mac, &bti);
        if (bti == ZX_HANDLE_INVALID) {
            status = ZX_ERR_INTERNAL;
            zxlogf(ERROR, "eth [%s]: ethmac_get_bti return invalid handle\n", edev->name);
            goto fail;
        }
        if ((status = zx_bti_pin(bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
                                 vmo, 0, size, edev->paddr_map, pages, &edev->pmt)) != ZX_OK) {
            zxlogf(ERROR, "eth [%s]: bti_pin failed, can't pin vmo: %d\n",
                   edev->name, status);
            zx_handle_close(bti);
            goto fail;
        }
        zx_handle_close(bti);
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
        // Re-acquire lock afterwards.
        mtx_unlock(&edev0->lock);
        status = ethmac_start(&edev->edev0->mac, edev0, &ethmac_ifc);
        mtx_lock(&edev0->lock);
        // Check whether unbind was called while we were unlocked.
        if (edev->state & ETHDEV_DEAD) {
            status = ZX_ERR_BAD_STATE;
        }
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
        zx_object_signal_peer(edev->rx_fifo, 0, fuchsia_hardware_ethernet_SIGNAL_STATUS);
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
                // Re-acquire lock afterwards.
                mtx_unlock(&edev0->lock);
                ethmac_stop(&edev->edev0->mac);
                mtx_lock(&edev0->lock);
            }
        }
    }

    return ZX_OK;
}

static ssize_t eth_set_client_name_locked(ethdev_t* edev, const void* in_buf, size_t in_len) {
    if (in_len >= sizeof(edev->name)) {
        in_len = sizeof(edev->name) - 1;
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
    if (zx_object_signal_peer(edev->rx_fifo, fuchsia_hardware_ethernet_SIGNAL_STATUS, 0) != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }

    uint32_t* status = out_buf;
    *status = edev->edev0->status;
    *out_actual = sizeof(*status);
    return ZX_OK;
}

#define REPLY(x) fuchsia_hardware_ethernet_Device##x##_reply

static zx_status_t fidl_GetInfo_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    fuchsia_hardware_ethernet_Info info;
    memset(&info, 0, sizeof(info));
    memcpy(info.mac.octets, edev->edev0->info.mac, ETH_MAC_SIZE);
    if (edev->edev0->info.features & ETHMAC_FEATURE_WLAN) {
        info.features |= fuchsia_hardware_ethernet_INFO_FEATURE_WLAN;
    }
    if (edev->edev0->info.features & ETHMAC_FEATURE_SYNTH) {
        info.features |= fuchsia_hardware_ethernet_INFO_FEATURE_SYNTH;
    }
    info.mtu = edev->edev0->info.mtu;
    return REPLY(GetInfo)(txn, &info);
}

static zx_status_t fidl_GetFifos_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    fuchsia_hardware_ethernet_Fifos fifos;
    return REPLY(GetFifos)(txn, eth_get_fifos_locked(edev, &fifos), &fifos);
}

static zx_status_t fidl_SetIOBuffer_locked(void* ctx, zx_handle_t h, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    return REPLY(SetIOBuffer)(txn, eth_set_iobuf_locked(edev, h));
}

static zx_status_t fidl_Start_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    return REPLY(Start)(txn, eth_start_locked(edev));
}

static zx_status_t fidl_Stop_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    eth_stop_locked(edev);
    return REPLY(Stop)(txn);
}

static zx_status_t fidl_ListenStart_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    return REPLY(ListenStart)(txn, eth_tx_listen_locked(edev, true));
}

static zx_status_t fidl_ListenStop_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    eth_tx_listen_locked(edev, false);
    return REPLY(ListenStop)(txn);
}

static zx_status_t fidl_SetClientName_locked(void* ctx, const char* buf, size_t len,
                                             fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    return REPLY(SetClientName)(txn, eth_set_client_name_locked(edev, buf, len));
}

static zx_status_t fidl_GetStatus_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    if (zx_object_signal_peer(edev->rx_fifo, fuchsia_hardware_ethernet_SIGNAL_STATUS, 0) != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }
    return REPLY(GetStatus)(txn, edev->edev0->status);
}

static zx_status_t fidl_SetPromisc_locked(void* ctx, bool enabled, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    return REPLY(SetPromiscuousMode)(txn, eth_set_promisc_locked(edev, enabled));
}

static zx_status_t
fidl_ConfigMulticastAddMac_locked(void* ctx, const fuchsia_hardware_ethernet_MacAddress* mac,
                                  fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    zx_status_t status = eth_add_multicast_address_locked(edev, mac->octets);
    return REPLY(ConfigMulticastAddMac)(txn, status);
}

static zx_status_t
fidl_ConfigMulticastDeleteMac_locked(void* ctx, const fuchsia_hardware_ethernet_MacAddress* mac,
                                     fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    zx_status_t status = eth_del_multicast_address_locked(edev, mac->octets);
    return REPLY(ConfigMulticastDeleteMac)(txn, status);
}

static zx_status_t fidl_ConfigMulticastSetPromiscuousMode_locked(void* ctx, bool enabled,
                                                                 fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    zx_status_t status = eth_set_multicast_promisc_locked(edev, enabled);
    return REPLY(ConfigMulticastSetPromiscuousMode)(txn, status);
}

static zx_status_t fidl_ConfigMulticastTestFilter_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    zxlogf(INFO,
           "MULTICAST_TEST_FILTER invoked. Turning multicast-promisc off unconditionally.\n");
    zx_status_t status = eth_test_clear_multicast_promisc_locked(edev);
    return REPLY(ConfigMulticastTestFilter)(txn, status);
}

static zx_status_t fidl_DumpRegisters_locked(void* ctx, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    zx_status_t status = ethmac_set_param(&edev->edev0->mac, ETHMAC_SETPARAM_DUMP_REGS, 0, NULL, 0);
    return REPLY(DumpRegisters)(txn, status);
}

#undef REPLY

fuchsia_hardware_ethernet_Device_ops_t fidl_ops = {
    .GetInfo = fidl_GetInfo_locked,
    .GetFifos = fidl_GetFifos_locked,
    .SetIOBuffer = fidl_SetIOBuffer_locked,
    .Start = fidl_Start_locked,
    .Stop = fidl_Stop_locked,
    .ListenStart = fidl_ListenStart_locked,
    .ListenStop = fidl_ListenStop_locked,
    .SetClientName = fidl_SetClientName_locked,
    .GetStatus = fidl_GetStatus_locked,
    .SetPromiscuousMode = fidl_SetPromisc_locked,
    .ConfigMulticastAddMac = fidl_ConfigMulticastAddMac_locked,
    .ConfigMulticastDeleteMac = fidl_ConfigMulticastDeleteMac_locked,
    .ConfigMulticastSetPromiscuousMode = fidl_ConfigMulticastSetPromiscuousMode_locked,
    .ConfigMulticastTestFilter = fidl_ConfigMulticastTestFilter_locked,
    .DumpRegisters = fidl_DumpRegisters_locked,
};

static zx_status_t eth_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    ethdev_t* edev = ctx;
    mtx_lock(&edev->edev0->lock);
    if (edev->state & ETHDEV_DEAD) {
        mtx_unlock(&edev->edev0->lock);
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = fuchsia_hardware_ethernet_Device_dispatch(ctx, txn, msg, &fidl_ops);
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
        free(edev->all_tx_bufs);
        free(edev->paddr_map);
    }
    free(edev);
}

static zx_status_t eth_open(void* ctx, zx_device_t** out, uint32_t flags) {
    ethdev_t* edev = ctx;
    mtx_lock(&edev->lock);
    edev->open_count++;
    mtx_unlock(&edev->lock);
    *out = NULL;
    return ZX_OK;
}

static zx_status_t eth_close(void* ctx, uint32_t flags) {
    ethdev_t* edev = ctx;

    bool destroy = false;
    mtx_lock(&edev->lock);
    edev->open_count--;
    if (edev->open_count == 0) {
        destroy = true;
    }
    mtx_unlock(&edev->lock);

    if (!destroy) {
        return ZX_OK;
    }

    mtx_lock(&edev->edev0->lock);
    eth_stop_locked(edev);
    eth_kill_locked(edev);
    list_delete(&edev->node);
    mtx_unlock(&edev->edev0->lock);

    return ZX_OK;
}

static zx_protocol_device_t ethdev_ops = {
    .version = DEVICE_OPS_VERSION,
    .open = eth_open,
    .close = eth_close,
    .message = eth_message,
    .release = eth_release,
};

static zx_status_t eth0_open(void* ctx, zx_device_t** out, uint32_t flags) {
    ethdev0_t* edev0 = ctx;

    ethdev_t* edev;
    if ((edev = calloc(1, sizeof(ethdev_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    edev->open_count = 1;
    edev->edev0 = edev0;

    edev->tx_size = ROUNDUP(sizeof(tx_info_t) + edev0->info.netbuf_size, 8);
    if ((edev->all_tx_bufs = calloc(FIFO_DEPTH, edev->tx_size)) == NULL) {
        free(edev);
        return ZX_ERR_NO_MEMORY;
    }

    list_initialize(&edev->free_tx_bufs);
    for (size_t ndx = 0; ndx < FIFO_DEPTH; ndx++) {
        ethmac_netbuf_t* netbuf =
                (ethmac_netbuf_t*)((uintptr_t)edev->all_tx_bufs + (edev->tx_size * ndx));
        tx_info_t* tx_info = netbuf_to_tx_info(edev0, netbuf);
        tx_info->edev = edev;
        list_add_tail(&edev->free_tx_bufs, &tx_info->node);
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
        free(edev->all_tx_bufs);
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
    if (device_get_protocol(dev, ZX_PROTOCOL_ETHMAC, &edev0->mac)) {
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

    if ((status = ethmac_query(&edev0->mac, 0, &edev0->info)) < 0) {
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

    if (edev0->info.netbuf_size < sizeof(ethmac_netbuf_t)) {
        zxlogf(ERROR, "eth: bind: device '%s': invalid buffer size %ld\n",
               device_get_name(dev), edev0->info.netbuf_size);
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }
    edev0->info.netbuf_size = ROUNDUP(edev0->info.netbuf_size, 8);

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
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ETHMAC),
ZIRCON_DRIVER_END(ethernet)
// clang-format on
