// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netifc.h"

#include <dirent.h>
#include <lib/fzl/vmo-mapper.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <thread>

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

#define NET_BUFFERS 256
#define NET_BUFFERSZ 2048

#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

#define ETH_BUFFER_FREE 0u    // on free list
#define ETH_BUFFER_TX 1u      // in tx ring
#define ETH_BUFFER_RX 2u      // in rx ring
#define ETH_BUFFER_CLIENT 3u  // in use by stack

constexpr size_t kBufferCount = 2ul * NET_BUFFERS;

struct eth_buffer {
  uint64_t magic;
  eth_buffer_t* next;
  void* data;
  uint32_t state;
};

namespace {

static_assert(sizeof(eth_buffer) == 32);

struct Netifc {
  std::mutex eth_lock;
  std::unique_ptr<EthClient> eth;
  uint8_t netmac[6];
  fuchsia_hardware_ethernet::wire::DeviceStatus last_dev_status;

  fzl::VmoMapper iobuf;
  std::array<eth_buffer_t, kBufferCount> eth_buffer_base;
  eth_buffer_t* eth_buffers __TA_GUARDED(eth_lock) = nullptr;

  void CheckEthBuf(eth_buffer_t* ethbuf, uint32_t state) {
    int check = [ethbuf, state, this]() {
      if ((eth_buffer_base.begin() > ethbuf) || (ethbuf >= eth_buffer_base.end())) {
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

  void PutBuffer(eth_buffer_t* buf, uint32_t state) __TA_REQUIRES(eth_lock) {
    CheckEthBuf(buf, state);
    buf->state = ETH_BUFFER_FREE;
    buf->next = eth_buffers;
    eth_buffers = buf;
  }

  zx_status_t GetBuffer(size_t sz, void** data, eth_buffer_t** out, uint32_t newstate, bool block)
      __TA_REQUIRES(eth_lock) {
    eth_buffer_t* buf;
    if (sz > NET_BUFFERSZ) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (eth_buffers == nullptr) {
      for (;;) {
        if (zx_status_t status = eth->CompleteTx(this,
                                                 [](void* ctx, void* cookie) {
                                                   Netifc& ifc = *static_cast<Netifc*>(ctx);
                                                   // Caled inline, eth_lock is held in containing
                                                   // method.
                                                   []() __TA_ASSERT(ifc.eth_lock) {}();
                                                   ifc.PutBuffer(static_cast<eth_buffer_t*>(cookie),
                                                                 ETH_BUFFER_TX);
                                                 });
            status != ZX_OK) {
          printf("%s: CompleteTx error: %s\n", __FUNCTION__, zx_status_get_string(status));
          return status;
        }
        if (eth_buffers != nullptr) {
          break;
        }
        if (!block) {
          return ZX_ERR_SHOULD_WAIT;
        }

        eth_lock.unlock();
        zx_status_t status = eth->WaitTx(zx::time::infinite());
        eth_lock.lock();

        if (status != ZX_OK) {
          return status;
        }
      }
    }
    buf = eth_buffers;
    eth_buffers = buf->next;
    buf->next = nullptr;

    CheckEthBuf(buf, ETH_BUFFER_FREE);

    buf->state = newstate;
    *data = buf->data;
    *out = buf;
    return ZX_OK;
  }
};

Netifc g_state;

}  // namespace

void eth_put_buffer(eth_buffer_t* ethbuf) {
  std::lock_guard lock(g_state.eth_lock);
  g_state.PutBuffer(ethbuf, ETH_BUFFER_CLIENT);
}

zx_status_t eth_get_buffer(size_t sz, void** data, eth_buffer_t** out, bool block) {
  std::lock_guard lock(g_state.eth_lock);
  return g_state.GetBuffer(sz, data, out, ETH_BUFFER_CLIENT, block);
}

zx_status_t eth_send(eth_buffer_t* ethbuf, size_t skip, size_t len) {
  std::lock_guard lock(g_state.eth_lock);

  g_state.CheckEthBuf(ethbuf, ETH_BUFFER_CLIENT);

#if DROP_PACKETS
  txc++;
  if ((random() % DROP_PACKETS) == 0) {
    printf("tx drop %d\n", txc);
    eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
    status = ZX_ERR_INTERNAL;
    goto fail;
  }
#endif

  if (g_state.eth == nullptr) {
    printf("eth_fifo_send: not connected\n");
    g_state.PutBuffer(ethbuf, ETH_BUFFER_CLIENT);
    return ZX_ERR_ADDRESS_UNREACHABLE;
  }

  ethbuf->state = ETH_BUFFER_TX;
  if (zx_status_t status =
          g_state.eth->QueueTx(ethbuf, static_cast<uint8_t*>(ethbuf->data) + skip, len);
      status != ZX_OK) {
    printf("eth_fifo_send: queue tx failed: %s\n", zx_status_get_string(status));
    g_state.PutBuffer(ethbuf, ETH_BUFFER_TX);
    return status;
  }

  return ZX_OK;
}

int eth_add_mcast_filter(const mac_addr_t* addr) { return 0; }

int netifc_open(const char* interface) {
  std::lock_guard lock(g_state.eth_lock);

  fidl::ClientEnd<fuchsia_hardware_ethernet::Device> device;
  // TODO: parameterize netsvc ethdir as well?
  if (zx_status_t status =
          netifc_discover("/dev/class/ethernet", interface,
                          device.channel().reset_and_get_address(), g_state.netmac);
      status != ZX_OK) {
    printf("netifc: failed to discover interface %s\n", zx_status_get_string(status));
    return -1;
  }

  // Allocate shareable ethernet buffer data heap.
  size_t iosize = 2ul * NET_BUFFERS * NET_BUFFERSZ;
  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(iosize, 0, &vmo); status != ZX_OK) {
    printf("netifc: create VMO failed: %s\n", zx_status_get_string(status));
    return status;
  }

  constexpr char kVmoName[] = "eth-buffers";
  if (zx_status_t status = vmo.set_property(ZX_PROP_NAME, kVmoName, sizeof(kVmoName));
      status != ZX_OK) {
    printf("netifc: set_property failed: %s\n", zx_status_get_string(status));
    return status;
  }

  fzl::VmoMapper mapper;
  if (zx_status_t status = mapper.Map(vmo, 0, iosize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      status != ZX_OK) {
    printf("netifc: map VMO failed: %s\n", zx_status_get_string(status));
    return status;
  }
  printf("netifc: create %zu eth buffers\n", g_state.eth_buffer_base.size());
  // Assign data chunks to ethbufs.
  for (size_t n = 0; n < g_state.eth_buffer_base.size(); n++) {
    eth_buffer_t& buffer = g_state.eth_buffer_base[n];
    buffer = {
        .magic = ETH_BUFFER_MAGIC,
        .data = static_cast<uint8_t*>(mapper.start()) + (n * NET_BUFFERSZ),
        .state = ETH_BUFFER_FREE,
    };
    g_state.PutBuffer(&buffer, ETH_BUFFER_FREE);
  }

  zx::status eth_status = EthClient::Create(std::move(device), std::move(vmo), mapper.start());
  if (eth_status.is_error()) {
    printf("EthClient::create() failed: %s\n", eth_status.status_string());
  }
  std::unique_ptr<EthClient> eth = std::move(eth_status.value());

  ip6_init(g_state.netmac, false);

  // Enqueue rx buffers.
  for (unsigned n = 0; n < NET_BUFFERS; n++) {
    void* data;
    eth_buffer_t* ethbuf;
    if (g_state.GetBuffer(NET_BUFFERSZ, &data, &ethbuf, ETH_BUFFER_RX, false)) {
      printf("netifc: only queued %u buffers (desired: %u)\n", n, NET_BUFFERS);
      break;
    }
    eth->QueueRx(ethbuf, ethbuf->data, NET_BUFFERSZ);
  }

  g_state.iobuf = std::move(mapper);
  g_state.eth = std::move(eth);

  return ZX_OK;
}

void netifc_close() {
  std::lock_guard lock(g_state.eth_lock);
  g_state.eth = nullptr;
  unsigned count = 0;
  for (auto& buff : g_state.eth_buffer_base) {
    switch (buff.state) {
      case ETH_BUFFER_FREE:
      case ETH_BUFFER_CLIENT:
        // on free list or owned by client
        // leave it alone
        break;
      case ETH_BUFFER_TX:
      case ETH_BUFFER_RX:
        // was sitting in ioring. reclaim.
        g_state.PutBuffer(&buff, buff.state);
        count++;
        break;
      default:
        printf("ethbuf %p: illegal state %u\n", &buff, buff.state);
        __builtin_trap();
        break;
    }
  }
  printf("netifc: recovered %u buffers\n", count);
}

int netifc_poll(zx_time_t deadline) {
  // Handle any completed rx packets
  if (zx_status_t status =
          g_state.eth->CompleteRx(&g_state,
                                  [](void* ctx, void* cookie, size_t len) {
                                    Netifc& netifc = *static_cast<Netifc*>(ctx);
                                    eth_buffer_t* ethbuf = static_cast<eth_buffer_t*>(cookie);
                                    netifc.CheckEthBuf(ethbuf, ETH_BUFFER_RX);
                                    netifc_recv(ethbuf->data, len);
                                    netifc.eth->QueueRx(ethbuf, ethbuf->data, NET_BUFFERSZ);
                                  });
      status != ZX_OK) {
    printf("netifc: eth rx failed: %s\n", zx_status_get_string(status));
    return -1;
  }

  if (netifc_send_pending()) {
    return 0;
  }

  {
    zx::status status = g_state.eth->WaitRx(zx::time(deadline));
    if (status.is_error()) {
      if (status.error_value() == ZX_ERR_TIMED_OUT) {
        // Deadline exceeded.
        return 0;
      }
      printf("netifc: eth rx wait failed: %s\n", status.status_string());
      return -1;
    }
    // No changes to device status.
    if (!status.value()) {
      return 0;
    }
  }

  // Device status was signaled, fetch new status.
  {
    zx::status status = g_state.eth->GetStatus();
    if (status.is_error()) {
      printf("netifc: error fetching device status %s\n", status.status_string());
      return -1;
    }
    fuchsia_hardware_ethernet::wire::DeviceStatus& device_status = status.value();
    if (device_status != g_state.last_dev_status) {
      if (device_status & fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline) {
        printf("netifc: Interface up.\n");
      } else {
        printf("netifc: Interface down.\n");
      }
      g_state.last_dev_status = device_status;
    }
  }

  return 0;
}
