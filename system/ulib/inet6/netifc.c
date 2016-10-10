// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <mxio/io.h>
#include <mxio/watcher.h>

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

static int netfd = -1;
static uint8_t netmac[6];

#define MAX_FILTER 8

#define NUM_BUFFER_PAGES 8
#define ETH_BUFFER_SIZE 1536
#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

typedef struct eth_buffer eth_buffer_t;
struct eth_buffer {
    uint64_t magic;
    eth_buffer_t* next;
    uint8_t data[0];
};

static eth_buffer_t* eth_buffers = NULL;

void* eth_get_buffer(size_t sz) {
    eth_buffer_t* buf;
    if (sz > ETH_BUFFER_SIZE) {
        return NULL;
    }
    if (eth_buffers == NULL) {
        printf("out of buffers\n");
        return NULL;
    }
    buf = eth_buffers;
    eth_buffers = buf->next;
    buf->next = NULL;
    return buf->data;
}

void eth_put_buffer(void* data) {
    eth_buffer_t* buf = (void*)(((uintptr_t)data) & (~31));
    if (buf->magic != ETH_BUFFER_MAGIC) {
        printf("fatal: eth buffer %p (from %p) bad magic %" PRIx64 "\n", buf, data, buf->magic);
        for (;;)
            ;
    }
    buf->next = eth_buffers;
    eth_buffers = buf;
}

int eth_send(void* data, size_t len) {
#if DROP_PACKETS
    txc++;
    if ((random() % DROP_PACKETS) == 0) {
        printf("tx drop %d\n", txc);
        eth_put_buffer(data);
        return len;
    }
#endif
    int r = write(netfd, data, len);
    eth_put_buffer(data);
    return r;
}

int eth_add_mcast_filter(const mac_addr_t* addr) {
    return 0;
}

static volatile uint64_t net_timer = 0;

void netifc_set_timer(uint32_t ms) {
    net_timer = mx_current_time() + MX_MSEC(ms);
}

int netifc_timer_expired(void) {
    if (net_timer == 0) {
        return 0;
    }
    if (mx_current_time() > net_timer) {
        return 1;
    }
    return 0;
}

static mx_status_t netifc_open_cb(int dirfd, const char* fn, void* cookie) {
    printf("netifc: ? /dev/class/ethernet/%s\n", fn);

    if ((netfd = openat(dirfd, fn, O_RDWR)) < 0) {
        return NO_ERROR;
    }

    if (read(netfd, netmac, 6) != 6) {
        close(netfd);
        netfd = -1;
        return NO_ERROR;
    }

    ip6_init(netmac);
    for (int i = 0; i < 8; i++) {
        char* buffer = malloc(sizeof(eth_buffer_t) + ETH_BUFFER_SIZE + 32);
        buffer = (char*)((((uintptr_t)buffer) + 31) & (~31));
        if (buffer) {
            eth_buffer_t* eb = (eth_buffer_t*)buffer;
            eb->magic = ETH_BUFFER_MAGIC;
            eth_put_buffer(buffer + sizeof(eth_buffer_t));
        }
    }

    // stop polling
    return 1;
}

int netifc_open(void) {
    int dirfd;
    if ((dirfd = open("/dev/class/ethernet", O_DIRECTORY|O_RDONLY)) < 0) {
        return -1;
    }

    mx_status_t status = mxio_watch_directory(dirfd, netifc_open_cb, NULL);
    close(dirfd);
    return (status < 0) ? -1 : 0;
}

void netifc_close(void) {
    close(netfd);
    netfd = -1;
}

int netifc_active(void) {
    return (netfd >= 0);
}

int netifc_poll(void) {
    uint8_t buffer[2048];
    mx_status_t r;

    for (;;) {
        while ((r = read(netfd, buffer, sizeof(buffer))) > 0) {
#if DROP_PACKETS
            rxc++;
            if ((random() % DROP_PACKETS) == 0) {
                printf("rx drop %d\n", rxc);
                continue;
            }
#endif
            eth_recv(buffer, r);
        }
        if (errno == ENOTCONN) {
            return -1;
        }
        if (net_timer) {
            mx_time_t now = mx_current_time();
            if (now > net_timer) {
                break;
            }
            mxio_wait_fd(netfd, MXIO_EVT_READABLE, NULL, net_timer - now + MX_MSEC(1));
        } else {
            mxio_wait_fd(netfd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        }
    }
    return 0;
}
