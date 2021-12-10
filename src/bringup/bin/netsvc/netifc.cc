// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netifc.h"

#include <dirent.h>
#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <lib/fit/defer.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include "src/bringup/bin/netsvc/eth-client.h"
#include "src/bringup/bin/netsvc/inet6.h"
#include "src/bringup/bin/netsvc/netifc-discover.h"

#define ALIGN(n, a) (((n) + ((a)-1)) & ~((a)-1))
// if nonzero, drop 1 in DROP_PACKETS packets at random
#define DROP_PACKETS 0

#if DROP_PACKETS > 0

// TODO: use libc random() once it's actually random

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

rand32_t rstate = {.n = 0x8716253};
#define random() rand32(&rstate)

static int txc;
static int rxc;
#endif

static mtx_t eth_lock = {};
static zx_handle_t g_netsvc = ZX_HANDLE_INVALID;
static eth_client_t* g_eth;
static uint8_t g_netmac[6];

static zx_handle_t iovmo;
static void* iobuf;

#define NET_BUFFERS 256
#define NET_BUFFERSZ 2048

#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

#define ETH_BUFFER_FREE 0u    // on free list
#define ETH_BUFFER_TX 1u      // in tx ring
#define ETH_BUFFER_RX 2u      // in rx ring
#define ETH_BUFFER_CLIENT 3u  // in use by stack

struct eth_buffer {
  uint64_t magic;
  eth_buffer_t* next;
  void* data;
  uint32_t state;
  uint32_t reserved;
};

static_assert(sizeof(eth_buffer_t) == 32);

static eth_buffer_t* eth_buffer_base;
static size_t eth_buffer_count;
static fuchsia_hardware_ethernet_DeviceStatus last_dev_status = 0;

static void check_ethbuf(eth_buffer_t* ethbuf, uint32_t state) {
  int check = [ethbuf, state]() {
    if (reinterpret_cast<uintptr_t>(ethbuf) & 31) {
      printf("ethbuf %p misaligned\n", ethbuf);
      return -1;
    }
    if ((ethbuf < eth_buffer_base) || (ethbuf >= (eth_buffer_base + eth_buffer_count))) {
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
  }();
  if (check) {
    __builtin_trap();
  }
}

static eth_buffer_t* eth_buffers = nullptr;

static void eth_put_buffer_locked(eth_buffer_t* buf, uint32_t state) __TA_REQUIRES(eth_lock) {
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

static void tx_complete(void* ctx, void* cookie) __TA_REQUIRES(eth_lock) {
  eth_put_buffer_locked(static_cast<eth_buffer_t*>(cookie), ETH_BUFFER_TX);
}

static zx_status_t eth_get_buffer_locked(size_t sz, void** data, eth_buffer_t** out,
                                         uint32_t newstate, bool block) __TA_REQUIRES(eth_lock) {
  eth_buffer_t* buf;
  if (sz > NET_BUFFERSZ) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (eth_buffers == nullptr) {
    for (;;) {
      eth_complete_tx(g_eth, nullptr, tx_complete);
      if (eth_buffers != nullptr) {
        break;
      }
      if (!block) {
        return ZX_ERR_SHOULD_WAIT;
      }
      zx_status_t status;
      zx_signals_t signals;
      mtx_unlock(&eth_lock);
      status = zx_object_wait_one(g_eth->tx_fifo, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                  ZX_TIME_INFINITE, &signals);
      mtx_lock(&eth_lock);
      if (status < 0) {
        return status;
      }
      if (signals & ZX_FIFO_PEER_CLOSED) {
        return ZX_ERR_PEER_CLOSED;
      }
    }
  }
  buf = eth_buffers;
  eth_buffers = buf->next;
  buf->next = nullptr;

  check_ethbuf(buf, ETH_BUFFER_FREE);

  buf->state = newstate;
  *data = buf->data;
  *out = buf;
  return ZX_OK;
}

zx_status_t eth_get_buffer(size_t sz, void** data, eth_buffer_t** out, bool block) {
  mtx_lock(&eth_lock);
  zx_status_t r = eth_get_buffer_locked(sz, data, out, ETH_BUFFER_CLIENT, block);
  mtx_unlock(&eth_lock);
  return r;
}

zx_status_t eth_send(eth_buffer_t* ethbuf, size_t skip, size_t len) {
  zx_status_t status;
  mtx_lock(&eth_lock);

  check_ethbuf(ethbuf, ETH_BUFFER_CLIENT);

#if DROP_PACKETS
  txc++;
  if ((random() % DROP_PACKETS) == 0) {
    printf("tx drop %d\n", txc);
    eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
    status = ZX_ERR_INTERNAL;
    goto fail;
  }
#endif

  if (g_eth == nullptr) {
    printf("eth_fifo_send: not connected\n");
    eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
    status = ZX_ERR_ADDRESS_UNREACHABLE;
    goto fail;
  }

  ethbuf->state = ETH_BUFFER_TX;
  status = eth_queue_tx(g_eth, ethbuf, static_cast<uint8_t*>(ethbuf->data) + skip, len, 0);
  if (status < 0) {
    printf("eth_fifo_send: queue tx failed: %d\n", status);
    eth_put_buffer_locked(ethbuf, ETH_BUFFER_TX);
    goto fail;
  }

  mtx_unlock(&eth_lock);
  return ZX_OK;

fail:
  mtx_unlock(&eth_lock);
  return status;
}

int eth_add_mcast_filter(const mac_addr_t* addr) { return 0; }

// TODO(https://fxbug.dev/89490): Re-enable thread safety analysis.
int netifc_open(const char* interface) __TA_NO_THREAD_SAFETY_ANALYSIS {
  mtx_lock(&eth_lock);

  auto defer_cleanup_netsvc = fit::defer([]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    zx_handle_close(g_netsvc);
    g_netsvc = ZX_HANDLE_INVALID;
    mtx_unlock(&eth_lock);
  });
  // TODO: parameterize netsvc ethdir as well?
  if (netifc_discover("/dev/class/ethernet", interface, &g_netsvc, g_netmac)) {
    return -1;
  }

  // we only do this the very first time
  if (eth_buffer_base == nullptr) {
    eth_buffer_base = static_cast<eth_buffer*>(
        memalign(sizeof(eth_buffer_t), 2ul * NET_BUFFERS * sizeof(eth_buffer_t)));
    if (eth_buffer_base == nullptr) {
      return -1;
    }
    eth_buffer_count = 2ul * NET_BUFFERS;
  }

  // we only do this the very first time
  if (iobuf == nullptr) {
    // allocate shareable ethernet buffer data heap
    size_t iosize = 2ul * NET_BUFFERS * NET_BUFFERSZ;
    if (zx_status_t status = zx_vmo_create(iosize, 0, &iovmo); status != ZX_OK) {
      return status;
    }
    zx_object_set_property(iovmo, ZX_PROP_NAME, "eth-buffers", 11);
    if (zx_status_t status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                                         iovmo, 0, iosize, reinterpret_cast<zx_vaddr_t*>(&iobuf));
        status != ZX_OK) {
      zx_handle_close(iovmo);
      iovmo = ZX_HANDLE_INVALID;
      return status;
    }
    printf("netifc: create %zu eth buffers\n", eth_buffer_count);
    // assign data chunks to ethbufs
    for (size_t n = 0; n < eth_buffer_count; n++) {
      eth_buffer_base[n].magic = ETH_BUFFER_MAGIC;
      eth_buffer_base[n].data = static_cast<uint8_t*>(iobuf) + (n * NET_BUFFERSZ);
      eth_buffer_base[n].state = ETH_BUFFER_FREE;
      eth_buffer_base[n].reserved = 0;
      eth_put_buffer_locked(eth_buffer_base + n, ETH_BUFFER_FREE);
    }
  }

  if (zx_status_t status = eth_create(g_netsvc, iovmo, iobuf, &g_eth); status != ZX_OK) {
    printf("eth_create() failed: %s\n", zx_status_get_string(status));
    return status;
  }

  auto defer_destroy_client = fit::defer([]() {
    eth_destroy(g_eth);
    g_eth = nullptr;
  });
  {
    zx_status_t call_status;
    if (zx_status_t status = fuchsia_hardware_ethernet_DeviceStart(g_netsvc, &call_status);
        status != ZX_OK) {
      printf("netifc: ethernet_impl_start(): %s\n", zx_status_get_string(status));
      return status;
    }
    if (call_status != ZX_OK) {
      printf("netifc: ethernet_impl_start() returned: %s\n", zx_status_get_string(call_status));
    }
  }

  ip6_init(g_netmac, false);

  // enqueue rx buffers
  for (unsigned n = 0; n < NET_BUFFERS; n++) {
    void* data;
    eth_buffer_t* ethbuf;
    if (eth_get_buffer_locked(NET_BUFFERSZ, &data, &ethbuf, ETH_BUFFER_RX, false)) {
      printf("netifc: only queued %u buffers (desired: %u)\n", n, NET_BUFFERS);
      break;
    }
    eth_queue_rx(g_eth, ethbuf, ethbuf->data, NET_BUFFERSZ, 0);
  }

  defer_cleanup_netsvc.cancel();
  defer_destroy_client.cancel();

  mtx_unlock(&eth_lock);
  return ZX_OK;
}

void netifc_close(void) {
  mtx_lock(&eth_lock);
  if (g_netsvc != ZX_HANDLE_INVALID) {
    zx_handle_close(g_netsvc);
    g_netsvc = ZX_HANDLE_INVALID;
  }
  if (g_eth != nullptr) {
    eth_destroy(g_eth);
    g_eth = nullptr;
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
        printf("ethbuf %p: illegal state %u\n", eth_buffer_base + n, eth_buffer_base[n].state);
        __builtin_trap();
        break;
    }
  }
  printf("netifc: recovered %u buffers\n", count);
  mtx_unlock(&eth_lock);
}

static void rx_complete(void* ctx, void* cookie, size_t len, uint32_t flags) {
  eth_buffer_t* ethbuf = static_cast<eth_buffer_t*>(cookie);
  check_ethbuf(ethbuf, ETH_BUFFER_RX);
  netifc_recv(ethbuf->data, len);
  eth_queue_rx(g_eth, ethbuf, ethbuf->data, NET_BUFFERSZ, 0);
}

int netifc_poll(zx_time_t deadline) {
  // Handle any completed rx packets
  zx_status_t status;
  if ((status = eth_complete_rx(g_eth, nullptr, rx_complete)) < 0) {
    printf("netifc: eth rx failed: %d\n", status);
    return -1;
  }

  if (netifc_send_pending()) {
    return 0;
  }
  fuchsia_hardware_ethernet_DeviceStatus dev_status = 0;
  zx_status_t fidl_status = fuchsia_hardware_ethernet_DeviceGetStatus(g_netsvc, &dev_status);
  if (dev_status != last_dev_status) {
    if (!(dev_status & fuchsia_hardware_ethernet_DeviceStatus_ONLINE)) {
      printf("netifc: Interface down. Operation code %i, FIDL code %s\n", dev_status,
             zx_status_get_string(fidl_status));
    } else {
      printf("netifc: Interface up.\n");
    }
    last_dev_status = dev_status;
  }

  status = eth_wait_rx(g_eth, deadline);
  if ((status < 0) && (status != ZX_ERR_TIMED_OUT)) {
    printf("netifc: eth rx wait failed: %d\n", status);
    return -1;
  }

  return 0;
}
