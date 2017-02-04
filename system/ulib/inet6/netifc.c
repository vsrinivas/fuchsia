// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

#include <eth/eth-fifo.h>
#include <magenta/device/ethernet.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>

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

static int netfd = -1;
static uint8_t netmac[6];
static size_t netmtu;
static bool use_fifo = false;
static mx_handle_t io_buf = MX_HANDLE_INVALID;
static eth_fifo_t fifo;
static uint8_t* rx_map = NULL;
static uint8_t* tx_map = NULL;
static eth_fifo_entry_t* rx_entries = NULL;
static eth_fifo_entry_t* tx_entries = NULL;

#define NET_BUFFERS 64
#define NET_BUFFERSZ 2048

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

static int eth_fifo_send(const void* data, size_t len) {
    mx_fifo_state_t state;
    mx_status_t status;
    for (;;) {
        if ((status = mx_fifo_op(fifo.tx_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
            printf("can't read tx_fifo state (%d)\n", status);
            return -1;
        }
        if ((state.head - state.tail) < NET_BUFFERS) {
            break;
        }
        mx_signals_t pending;
        if ((status = mx_handle_wait_one(fifo.tx_fifo, MX_FIFO_NOT_FULL | MX_FIFO_CONSUMER_EXCEPTION,
                                         MX_TIME_INFINITE, &pending)) < 0) {
            printf("wait on tx_fifo failed (%d)\n", status);
            return -1;
        }
        if (pending & MX_FIFO_CONSUMER_EXCEPTION) {
            return -1;
        }
    }
    uint64_t entry_idx = state.head & (NET_BUFFERS - 1);
    uint64_t offset = NET_BUFFERSZ * entry_idx;
    memcpy(&tx_map[offset], data, len);
    eth_fifo_entry_t* entry = &tx_entries[entry_idx];
    entry->offset = offset;
    entry->length = len;
    status = mx_fifo_op(fifo.tx_fifo, MX_FIFO_OP_ADVANCE_HEAD, 1u, NULL);
    return status == NO_ERROR ? (int)len : -1;
}

static int eth_fifo_send_r(const void* data, size_t len) {
    static mtx_t lock = MTX_INIT;
    mtx_lock(&lock);
    mx_status_t status = eth_fifo_send(data, len);
    mtx_unlock(&lock);
    return status;
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
    int r;
    if (use_fifo) {
        r = eth_fifo_send_r(data, len);
    } else {
        r = write(netfd, data, len);
    }
    eth_put_buffer(data);
    return r;
}

void netifc_send(const void* data, size_t len) {
    if (use_fifo) {
        eth_fifo_send_r(data, len);
    } else {
        write(netfd, data, len);
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

static mx_status_t netifc_open_cb(int dirfd, const char* fn, void* cookie) {
    printf("netifc: ? /dev/class/ethernet/%s\n", fn);

    if ((netfd = openat(dirfd, fn, O_RDWR)) < 0) {
        return NO_ERROR;
    }

    if (ioctl_ethernet_get_mac_addr(netfd, netmac, sizeof(netmac)) < 0) {
      close(netfd);
      netfd = -1;
      return NO_ERROR;
    }

    if (ioctl_ethernet_get_mtu(netfd, &netmtu) < 0) {
      close(netfd);
      netfd = -1;
      return NO_ERROR;
    }
    ip6_init(netmac);

    eth_get_fifo_args_t args = { .options = 0, .rx_entries = NET_BUFFERS, .tx_entries = NET_BUFFERS };
    ssize_t fifo_ret = ioctl_ethernet_get_fifo(netfd, &args, &fifo);
    if (fifo_ret == ERR_NOT_SUPPORTED) {
        use_fifo = false;
    } else if (fifo_ret < 0) {
        printf("%s could not get fifo from driver\n", __func__);
        netifc_close();
        return NO_ERROR;
    } else {
        printf("%s using fifo\n", __func__);
        use_fifo = true;
    }

    if (use_fifo) {
        // Both rx and tx will have NET_BUFFERS buffers of size NET_BUFFERSZ.
        // Align each set of buffers to PAGE_SIZE for ease of mapping.
        size_t io_buf_size = ALIGN(NET_BUFFERS * NET_BUFFERSZ, PAGE_SIZE);
        mx_status_t status = mx_vmo_create(2 * io_buf_size, 0, &io_buf);
        if (status != NO_ERROR) {
            printf("%s could not create fifo io vmo (%d)\n", __func__, status);
            netifc_close();
            return NO_ERROR;
        }

        mx_handle_t io_buf_dup;
        if (mx_handle_duplicate(io_buf, MX_RIGHT_SAME_RIGHTS, &io_buf_dup) != NO_ERROR) {
            printf("%s could not duplicate io vmo\n", __func__);
            netifc_close();
            return NO_ERROR;
        }
        eth_set_io_buf_args_t io_buf_args = {
            .io_buf_vmo = io_buf_dup,
            .rx_offset = 0,
            .rx_len = io_buf_size,
            .tx_offset = io_buf_size,
            .tx_len = io_buf_size,
        };
        if (ioctl_ethernet_set_io_buf(netfd, &io_buf_args) < 0) {
            printf("%s could not set io vmo in driver\n", __func__);
            mx_handle_close(io_buf_dup);
            netifc_close();
            return NO_ERROR;
        }

        status = mx_vmar_map(mx_vmar_root_self(), 0, io_buf, 0, io_buf_size,
                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)&rx_map);
        if (status != NO_ERROR) {
            printf("%s could not map rx buf vmo (%d)\n", __func__, status);
            netifc_close();
            return NO_ERROR;
        }
        status = mx_vmar_map(mx_vmar_root_self(), 0, io_buf, io_buf_size, io_buf_size,
                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)&tx_map);
        if (status != NO_ERROR) {
            printf("%s could not map tx buf vmo (%d)\n", __func__, status);
            netifc_close();
            return NO_ERROR;
        }

        status = eth_fifo_map_rx_entries(&fifo, &rx_entries);
        if (status != NO_ERROR) {
            printf("%s could not map rx entries vmo (%d)\n", __func__, status);
            netifc_close();
            return NO_ERROR;
        }

        status = eth_fifo_map_tx_entries(&fifo, &tx_entries);
        if (status != NO_ERROR) {
            printf("%s could not map rx entries vmo (%d)\n", __func__, status);
            netifc_close();
            return NO_ERROR;
        }

        eth_fifo_entry_t* entry = rx_entries;
        for (int i = 0; i < NET_BUFFERS; i++) {
            entry->offset = i * NET_BUFFERSZ;
            entry->length = NET_BUFFERSZ;
            entry->flags = 0;
            entry->cookie = 0;
            entry++;
        }
        status = mx_fifo_op(fifo.rx_fifo, MX_FIFO_OP_ADVANCE_HEAD, NET_BUFFERS, NULL);
        if (status != NO_ERROR) {
            printf("%s could not advance rx fifo head (%d)\n", __func__, status);
            netifc_close();
            return NO_ERROR;
        }

        if ((status = ioctl_ethernet_start(netfd)) < 0) {
            printf("netifc: ethernet_start(): %d\n", status);
            //TODO: make fatal once all drivers support this
        }
    }

    for (int i = 0; i < NET_BUFFERS; i++) {
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
    if (use_fifo) {
        // TODO: reset the driver
        printf("%s closing netifc, cleaning up fifo\n", __func__);
        if (tx_entries) {
            mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)tx_entries, 0);
        }
        if (rx_entries) {
            mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)rx_entries, 0);
        }
        if (rx_map) {
            mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)rx_map, 0);
        }
        if (tx_map) {
            mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)tx_map, 0);
        }
        eth_fifo_cleanup(&fifo);
        if (io_buf != MX_HANDLE_INVALID) {
            mx_handle_close(io_buf);
            io_buf = MX_HANDLE_INVALID;
        }
    }
    close(netfd);
    netfd = -1;
    use_fifo = false;
}

int netifc_active(void) {
    return (netfd >= 0);
}

static int netifc_fifo_poll(void) {
    mx_status_t status;
    mx_fifo_state_t state;
    eth_fifo_entry_t* entry;
    for (;;) {
        status = mx_fifo_op(fifo.rx_fifo, MX_FIFO_OP_READ_STATE, 0, &state);
        if (status != NO_ERROR) {
            printf("%s could not read fifo state (%d)\n", __func__, status);
            return -1;
        }
        uint64_t entries = state.head - state.tail;
        while (entries < NET_BUFFERS) {
            uint64_t entry_idx = state.head & (NET_BUFFERS - 1);
            entry = &rx_entries[entry_idx];
            netifc_recv(&rx_map[entry->offset], entry->length);
            // requeue entry
            // TODO: batch these up
            entry->length = NET_BUFFERSZ;
            status = mx_fifo_op(fifo.rx_fifo, MX_FIFO_OP_ADVANCE_HEAD, 1u, &state);
            if (status != NO_ERROR) {
                printf("%s could not advance fifo rx head (%d)\n", __func__, status);
                return -1;
            }
            entries = state.head - state.tail;
        }
        // TODO: detect ENOTCONN
        if (netfd == -1) {
            printf("%s netfd < 0, stopping poll\n", __func__);
            return -1;
        }
        mx_signals_t pending = 0;
        if (net_timer) {
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
            if (now > net_timer) {
                break;
            }
            mx_handle_wait_one(fifo.rx_fifo, MX_FIFO_NOT_FULL | MX_FIFO_CONSUMER_EXCEPTION,
                               net_timer - now + MX_MSEC(1), &pending);
        } else {
            mx_handle_wait_one(fifo.rx_fifo, MX_FIFO_NOT_FULL | MX_FIFO_CONSUMER_EXCEPTION,
                               MX_TIME_INFINITE, &pending);
        }
        if (pending & MX_FIFO_CONSUMER_EXCEPTION) {
            return -1;
        }
    }
    return 0;
}

int netifc_poll(void) {
    uint8_t buffer[NET_BUFFERSZ];
    mx_status_t r;

    if (use_fifo) {
        return netifc_fifo_poll();
    }

    for (;;) {
        while ((r = read(netfd, buffer, sizeof(buffer))) > 0) {
#if DROP_PACKETS
            rxc++;
            if ((random() % DROP_PACKETS) == 0) {
                printf("rx drop %d\n", rxc);
                continue;
            }
#endif
            netifc_recv(buffer, r);
        }
        if (errno == ENOTCONN) {
            return -1;
        }
        if (net_timer) {
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
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
