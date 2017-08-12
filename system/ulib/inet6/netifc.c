// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <threads.h>

#include "eth-client.h"

#include <magenta/device/ethernet.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <mxio/io.h>
#include <mxio/watcher.h>

#define ALIGN(n, a) (((n) + ((a) - 1)) & ~((a) - 1))
// if nonzero, drop 1 in DROP_PACKETS packets at random
#define DROP_PACKETS 0

#if DROP_PACKETS > 0

//TODO: use libc random() once it's actually random

// Xorshift32 prng
typedef struct {
    uint32_t n;
} rand32_t;

static inline uint32_t rand32(rand32_t* state) {
    uint32_t n = state->n;
    n ^= (n << 13);
    n ^= (n >> 17);
    n ^= (n << 5);
    return (state->n = n);
}

rand32_t rstate = { .n = 0x8716253 };
#define random() rand32(&rstate)

static int txc;
static int rxc;
#endif

static mtx_t eth_lock = MTX_INIT;
static int netfd = -1;
static eth_client_t* eth;
static uint8_t netmac[6];
static size_t netmtu;

static mx_handle_t iovmo;
static void* iobuf;

#define NET_BUFFERS 64
#define NET_BUFFERSZ 2048

#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

#define ETH_BUFFER_FREE   0u // on free list
#define ETH_BUFFER_TX     1u // in tx ring
#define ETH_BUFFER_RX     2u // in rx ring
#define ETH_BUFFER_CLIENT 3u // in use by stack

typedef struct eth_buffer eth_buffer_t;

struct eth_buffer {
    uint64_t magic;
    eth_buffer_t* next;
    void* data;
    uint32_t state;
    uint32_t reserved;
};

static_assert(sizeof(eth_buffer_t) == 32, "");

static eth_buffer_t* eth_buffer_base;
static size_t eth_buffer_count;

static int _check_ethbuf(eth_buffer_t* ethbuf, uint32_t state) {
    if (((uintptr_t) ethbuf) & 31) {
        printf("ethbuf %p misaligned\n", ethbuf);
        return -1;
    }
    if ((ethbuf < eth_buffer_base) ||
        (ethbuf >= (eth_buffer_base + eth_buffer_count))) {
        printf("ethbuf %p outside of arena\n", ethbuf);
        return -1;
    }
    if (ethbuf->magic != ETH_BUFFER_MAGIC) {
        printf("ethbuf %p bad magic\n", ethbuf);
        return -1;
    }
    if (ethbuf->state != state) {
        printf("ethbuf %p incorrect state (%u != %u)\n", ethbuf, ethbuf->state, state);
        return -1;
    }
    return 0;
}

static void check_ethbuf(eth_buffer_t* ethbuf, uint32_t state) {
    if (_check_ethbuf(ethbuf, state)) {
        __builtin_trap();
    }
}

static eth_buffer_t* eth_buffers = NULL;

static int eth_get_buffer_locked(size_t sz, void** data, eth_buffer_t** out, uint32_t newstate) {
    eth_buffer_t* buf;
    if (sz > NET_BUFFERSZ) {
        return -1;
    }
    if (eth_buffers == NULL) {
        printf("eth: get_buffer: out of buffers\n");
        return -1;
    }
    buf = eth_buffers;
    eth_buffers = buf->next;
    buf->next = NULL;

    check_ethbuf(buf, ETH_BUFFER_FREE);

    buf->state = newstate;
    *data = buf->data;
    *out = buf;
    return 0;
}

int eth_get_buffer(size_t sz, void** data, eth_buffer_t** out) {
    mtx_lock(&eth_lock);
    int r = eth_get_buffer_locked(sz, data, out, ETH_BUFFER_CLIENT);
    mtx_unlock(&eth_lock);
    return r;
}

static void eth_put_buffer_locked(eth_buffer_t* buf, uint32_t state) {
    check_ethbuf(buf, state);
    buf->state = ETH_BUFFER_FREE;
    buf->next = eth_buffers;
    eth_buffers = buf;
}

void eth_put_buffer(eth_buffer_t* ethbuf) {
    mtx_lock(&eth_lock);
    eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
    mtx_unlock(&eth_lock);
}

static void tx_complete(void* ctx, void* cookie) {
    eth_put_buffer_locked(cookie, ETH_BUFFER_TX);
}

int eth_send(eth_buffer_t* ethbuf, size_t skip, size_t len) {
    mtx_lock(&eth_lock);

    check_ethbuf(ethbuf, ETH_BUFFER_CLIENT);

#if DROP_PACKETS
    txc++;
    if ((random() % DROP_PACKETS) == 0) {
        printf("tx drop %d\n", txc);
        eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
        goto fail;
    }
#endif

    if (eth == NULL) {
        printf("eth_fifo_send: not connected\n");
        eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
        goto fail;
    }

    eth_complete_tx(eth, NULL, tx_complete);

    ethbuf->state = ETH_BUFFER_TX;
    mx_status_t status = eth_queue_tx(eth, ethbuf, ethbuf->data + skip, len, 0);
    if (status < 0) {
        printf("eth_fifo_send: queue tx failed: %d\n", status);
        eth_put_buffer_locked(ethbuf, ETH_BUFFER_TX);
        goto fail;
    }

    mtx_unlock(&eth_lock);
    return 0;

fail:
    mtx_unlock(&eth_lock);
    return -1;
}

//TODO: expose ethbuf to netifc clients, avoid a copy here
void netifc_send(const void* _data, size_t len) {
    void* data;
    eth_buffer_t* ethbuf;
    if (eth_get_buffer(len, &data, &ethbuf) == 0) {
        memcpy(data, _data, len);
        eth_send(ethbuf, 0, len);
    }
}

int eth_add_mcast_filter(const mac_addr_t* addr) {
    return 0;
}

static volatile uint64_t net_timer = 0;

void netifc_set_timer(uint32_t ms) {
    net_timer = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(ms);
}

int netifc_timer_expired(void) {
    if (net_timer == 0) {
        return 0;
    }
    if (mx_time_get(MX_CLOCK_MONOTONIC) > net_timer) {
        return 1;
    }
    return 0;
}

void netifc_get_info(uint8_t* addr, uint16_t* mtu) {
    memcpy(addr, netmac, 6);
    *mtu = netmtu;
}

static mx_status_t netifc_open_cb(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return MX_OK;
    }

    printf("netifc: ? /dev/class/ethernet/%s\n", fn);

    if ((netfd = openat(dirfd, fn, O_RDWR)) < 0) {
        return MX_OK;
    }

    eth_info_t info;
    if (ioctl_ethernet_get_info(netfd, &info) < 0) {
        close(netfd);
        netfd = -1;
        return MX_OK;
    }
    if (info.features & (ETH_FEATURE_WLAN | ETH_FEATURE_SYNTH)) {
        // Don't run netsvc for wireless or synthetic network devices
        close(netfd);
        netfd = -1;
        return MX_OK;
    }
    memcpy(netmac, info.mac, sizeof(netmac));
    netmtu = info.mtu;

    mtx_lock(&eth_lock);
    mx_status_t status;

    // we only do this the very first time
    if (eth_buffer_base == NULL) {
        eth_buffer_base = memalign(sizeof(eth_buffer_t), 2 * NET_BUFFERS * sizeof(eth_buffer_t));
        if (eth_buffer_base == NULL) {
            goto fail_close_fd;
        }
        eth_buffer_count = 2 * NET_BUFFERS;
    }

    // we only do this the very first time
    if (iobuf == NULL) {
        // allocate shareable ethernet buffer data heap
        size_t iosize = 2 * NET_BUFFERS * NET_BUFFERSZ;
        if ((status = mx_vmo_create(iosize, 0, &iovmo)) < 0) {
            goto fail_close_fd;
        }
        if ((status = mx_vmar_map(mx_vmar_root_self(), 0, iovmo, 0, iosize,
                                  MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                  (uintptr_t*)&iobuf)) < 0) {
            mx_handle_close(iovmo);
            iovmo = MX_HANDLE_INVALID;
            goto fail_close_fd;
        }
        printf("netifc: create %zu eth buffers\n", eth_buffer_count);
        // assign data chunks to ethbufs
        for (unsigned n = 0; n < eth_buffer_count; n++) {
            eth_buffer_base[n].magic = ETH_BUFFER_MAGIC;
            eth_buffer_base[n].data = iobuf + n * NET_BUFFERSZ;
            eth_buffer_base[n].state = ETH_BUFFER_FREE;
            eth_buffer_base[n].reserved = 0;
            eth_put_buffer_locked(eth_buffer_base + n, ETH_BUFFER_FREE);
        }
    }

    status = eth_create(netfd, iovmo, iobuf, &eth);
    if (status < 0) {
        printf("eth_create() failed: %d\n", status);
        goto fail_close_fd;
    }

    if ((status = ioctl_ethernet_start(netfd)) < 0) {
        printf("netifc: ethernet_start(): %d\n", status);
        goto fail_destroy_client;
    }

    ip6_init(netmac);

    // enqueue rx buffers
    for (unsigned n = 0; n < NET_BUFFERS; n++) {
        void* data;
        eth_buffer_t* ethbuf;
        if (eth_get_buffer_locked(NET_BUFFERSZ, &data, &ethbuf, ETH_BUFFER_RX)) {
            printf("netifc: only queued %u buffers (desired: %u)\n", n, NET_BUFFERS);
            break;
        }
        eth_queue_rx(eth, ethbuf, ethbuf->data, NET_BUFFERSZ, 0);
    }

    mtx_unlock(&eth_lock);

    // stop polling
    return MX_ERR_STOP;

fail_destroy_client:
    eth_destroy(eth);
    eth = NULL;
fail_close_fd:
    close(netfd);
    netfd = -1;
    mtx_unlock(&eth_lock);
    return MX_OK;
}

int netifc_open(void) {
    int dirfd;
    if ((dirfd = open("/dev/class/ethernet", O_DIRECTORY|O_RDONLY)) < 0) {
        return -1;
    }

    mx_status_t status = mxio_watch_directory(dirfd, netifc_open_cb, MX_TIME_INFINITE, NULL);
    close(dirfd);

    // callback returns STOP if it finds and successfully
    // opens a network interface
    return (status == MX_ERR_STOP) ? 0 : -1;
}

void netifc_close(void) {
    mtx_lock(&eth_lock);
    if (netfd != -1) {
        close(netfd);
        netfd = -1;
    }
    if (eth != NULL) {
        eth_destroy(eth);
        eth = NULL;
    }
    unsigned count = 0;
    for (unsigned n = 0; n < eth_buffer_count; n++) {
        switch (eth_buffer_base[n].state) {
        case ETH_BUFFER_FREE:
        case ETH_BUFFER_CLIENT:
            // on free list or owned by client
            // leave it alone
            break;
        case ETH_BUFFER_TX:
        case ETH_BUFFER_RX:
            // was sitting in ioring. reclaim.
            eth_put_buffer_locked(eth_buffer_base + n, eth_buffer_base[n].state);
            count++;
            break;
        default:
            printf("ethbuf %p: illegal state %u\n",
                   eth_buffer_base + n, eth_buffer_base[n].state);
            __builtin_trap();
            break;
        }
    }
    printf("netifc: recovered %u buffers\n", count);
    mtx_unlock(&eth_lock);
}

static void rx_complete(void* ctx, void* cookie, size_t len, uint32_t flags) {
    eth_buffer_t* ethbuf = cookie;
    check_ethbuf(ethbuf, ETH_BUFFER_RX);
    netifc_recv(ethbuf->data, len);
    eth_queue_rx(eth, ethbuf, ethbuf->data, NET_BUFFERSZ, 0);
}

int netifc_poll(void) {
    for (;;) {
        mx_status_t status;
        if ((status = eth_complete_rx(eth, NULL, rx_complete)) < 0) {
            printf("netifc: eth rx failed: %d\n", status);
            return -1;
        }
        if (net_timer) {
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
            if (now > net_timer) {
                return 0;
            }
            status = eth_wait_rx(eth, net_timer + MX_MSEC(1));
        } else {
            status = eth_wait_rx(eth, MX_TIME_INFINITE);
        }
        if ((status < 0) && (status != MX_ERR_TIMED_OUT)) {
            printf("netifc: eth rx wait failed: %d\n", status);
            return -1;
        }
    }
}

