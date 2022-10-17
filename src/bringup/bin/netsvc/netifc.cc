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
  eth_buffer* next;
  void* data;
  uint32_t state;
};

namespace {

// Helpers to visit on device variants.
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

static_assert(sizeof(eth_buffer) == 32);

struct Netifc {
  virtual ~Netifc() = default;
  virtual zx::result<DeviceBuffer> GetBuffer(size_t len, bool block) = 0;
};

struct EthNetifc : Netifc {
  ~EthNetifc() override = default;

  std::mutex eth_lock;
  std::unique_ptr<EthClient> eth;
  fuchsia_hardware_ethernet::wire::DeviceStatus last_dev_status;
  fit::callback<void(zx_status_t)> on_error;

  fzl::VmoMapper iobuf;
  std::array<eth_buffer, kBufferCount> eth_buffer_base;
  eth_buffer* eth_buffers __TA_GUARDED(eth_lock) = nullptr;

  void CheckEthBuf(eth_buffer* ethbuf, uint32_t state) {
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

  void PutBuffer(eth_buffer* buf, uint32_t state) __TA_REQUIRES(eth_lock) {
    CheckEthBuf(buf, state);
    buf->state = ETH_BUFFER_FREE;
    buf->next = eth_buffers;
    eth_buffers = buf;
  }

  zx::result<eth_buffer*> GetBuffer(size_t sz, uint32_t newstate, bool block)
      __TA_REQUIRES(eth_lock) {
    eth_buffer* buf;
    if (sz > NET_BUFFERSZ) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (eth_buffers == nullptr) {
      for (;;) {
        if (zx_status_t status = eth->CompleteTx(this,
                                                 [](void* ctx, void* cookie) {
                                                   EthNetifc& ifc = *static_cast<EthNetifc*>(ctx);
                                                   // Caled inline, eth_lock is held in containing
                                                   // method.
                                                   []() __TA_ASSERT(ifc.eth_lock) {}();
                                                   ifc.PutBuffer(static_cast<eth_buffer*>(cookie),
                                                                 ETH_BUFFER_TX);
                                                 });
            status != ZX_OK) {
          printf("%s: CompleteTx error: %s\n", __FUNCTION__, zx_status_get_string(status));
          return zx::error(status);
        }
        if (eth_buffers != nullptr) {
          break;
        }
        if (!block) {
          return zx::error(ZX_ERR_SHOULD_WAIT);
        }

        eth_lock.unlock();
        zx_status_t status = eth->WaitTx(zx::time::infinite());
        eth_lock.lock();

        if (status != ZX_OK) {
          return zx::error(status);
        }
      }
    }
    buf = eth_buffers;
    eth_buffers = buf->next;
    buf->next = nullptr;

    CheckEthBuf(buf, ETH_BUFFER_FREE);

    buf->state = newstate;
    return zx::ok(buf);
  }

  zx::result<DeviceBuffer> GetBuffer(size_t sz, bool block) override {
    std::lock_guard lock(eth_lock);
    zx::result status = GetBuffer(sz, ETH_BUFFER_CLIENT, block);
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(DeviceBuffer(status.value()));
  }

  void HandleRx() {
    // Handle any completed rx packets
    zx_status_t status = eth->CompleteRx(
        this, [](async_dispatcher_t* dispatcher, void* ctx, void* cookie, size_t len) {
          EthNetifc& netifc = *static_cast<EthNetifc*>(ctx);
          eth_buffer* ethbuf = static_cast<eth_buffer*>(cookie);
          netifc.CheckEthBuf(ethbuf, ETH_BUFFER_RX);
          netifc_recv(dispatcher, ethbuf->data, len);
          netifc.eth->QueueRx(ethbuf, ethbuf->data, NET_BUFFERSZ);
        });
    if (status != ZX_OK) {
      printf("netifc: eth rx failed: %s\n", zx_status_get_string(status));
      on_error(status);
    }
  }

  void HandleSignal() {
    // Device status was signaled, fetch new status.
    zx::result status = eth->GetStatus();
    if (status.is_error()) {
      printf("netifc: error fetching device status %s\n", status.status_string());
      on_error(status.status_value());
    }
    fuchsia_hardware_ethernet::wire::DeviceStatus& device_status = status.value();
    if (device_status != last_dev_status) {
      if (device_status & fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline) {
        printf("netifc: Interface up.\n");
      } else {
        printf("netifc: Interface down.\n");
      }
      last_dev_status = device_status;
    }
  }

  void HandleClosed() {
    printf("netfic: device closed\n");
    on_error(ZX_ERR_PEER_CLOSED);
  }
};

struct NetdeviceIfc : Netifc {
  NetdeviceIfc(fidl::ClientEnd<fuchsia_hardware_network::Device> device,
               async_dispatcher_t* dispatcher, fit::callback<void(zx_status_t)> on_error,
               fuchsia_hardware_network::wire::PortId port_id)
      : client(std::move(device), dispatcher), port_id(port_id), on_error(std::move(on_error)) {}

  zx::result<DeviceBuffer> GetBuffer(size_t len, bool block) override {
    network::client::NetworkDeviceClient::Buffer tx = client.AllocTx();
    if (!tx.is_valid()) {
      // Be loud in case the caller expects this to be synchronous, we can
      // change strategies if this proves a problem.
      if (block) {
        printf("netifc: netdevice does not block for new buffers, transfer will fail\n");
      }
      return zx::error(ZX_ERR_NO_RESOURCES);
    }
    if (len > tx.data().part(0).len()) {
      printf("netifc: can't allocate %zu bytes, buffer is %d\n", len, tx.data().part(0).len());
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::ok(std::move(tx));
  }

  network::client::NetworkDeviceClient client;
  const fuchsia_hardware_network::wire::PortId port_id;
  fit::callback<void(zx_status_t)> on_error;
};

std::unique_ptr<Netifc> g_state;

}  // namespace

DeviceBuffer::DeviceBuffer(Contents contents) : contents_(std::move(contents)) {}
cpp20::span<uint8_t> DeviceBuffer::data() {
  return std::visit(
      overloaded{
          [](std::monostate&) -> cpp20::span<uint8_t> {
            return cpp20::span(static_cast<uint8_t*>(nullptr), 0);
          },
          [](eth_buffer* b) -> cpp20::span<uint8_t> {
            return cpp20::span(static_cast<uint8_t*>(b->data), NET_BUFFERSZ);
          },
          [](network::client::NetworkDeviceClient::Buffer& b) -> cpp20::span<uint8_t> {
            return b.data().part(0).data();
          },
      },
      contents_);
}

zx::result<DeviceBuffer> DeviceBuffer::Get(size_t len, bool block) {
  if (g_state == nullptr) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return g_state->GetBuffer(len, block);
}

zx_status_t DeviceBuffer::Send(size_t len) {
  if (g_state == nullptr) {
    printf("%s: no state?\n", __func__);
    return ZX_ERR_BAD_STATE;
  }
  return std::visit(
      overloaded{
          [](std::monostate&) -> zx_status_t { return ZX_ERR_BAD_STATE; },
          [len](eth_buffer* ethbuf) -> zx_status_t {
            EthNetifc& eth_state = static_cast<EthNetifc&>(*g_state);
            std::lock_guard lock(eth_state.eth_lock);

            eth_state.CheckEthBuf(ethbuf, ETH_BUFFER_CLIENT);

#if DROP_PACKETS
            txc++;
            if ((random() % DROP_PACKETS) == 0) {
              printf("tx drop %d\n", txc);
              eth_put_buffer_locked(ethbuf, ETH_BUFFER_CLIENT);
              status = ZX_ERR_INTERNAL;
              goto fail;
            }
#endif

            if (eth_state.eth == nullptr) {
              printf("eth_fifo_send: not connected\n");
              eth_state.PutBuffer(ethbuf, ETH_BUFFER_CLIENT);
              return ZX_ERR_ADDRESS_UNREACHABLE;
            }

            ethbuf->state = ETH_BUFFER_TX;
            if (zx_status_t status =
                    eth_state.eth->QueueTx(ethbuf, static_cast<uint8_t*>(ethbuf->data), len);
                status != ZX_OK) {
              printf("eth_fifo_send: queue tx failed: %s\n", zx_status_get_string(status));
              eth_state.PutBuffer(ethbuf, ETH_BUFFER_TX);
              return status;
            }

            return ZX_OK;
          },
          [len](network::client::NetworkDeviceClient::Buffer& b) -> zx_status_t {
            b.data().part(0).CapLength(static_cast<uint32_t>(len));
            b.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
            b.data().SetPortId(static_cast<NetdeviceIfc&>(*g_state).port_id);
            zx_status_t ret = b.Send();
            if (ret != ZX_OK) {
              printf("%s: Send failed: %s", __func__, zx_status_get_string(ret));
            }
            return ret;
          },
      },
      contents_);
}

int eth_add_mcast_filter(const mac_addr_t* addr) { return 0; }

zx::result<> open_ethernet(async_dispatcher_t* dispatcher,
                           fidl::ClientEnd<fuchsia_hardware_ethernet::Device> device,
                           fit::callback<void(zx_status_t)> on_error) {
  std::unique_ptr state = std::make_unique<EthNetifc>();
  EthNetifc& eth_state = *state;
  std::lock_guard lock(eth_state.eth_lock);

  eth_state.on_error = std::move(on_error);
  // Allocate shareable ethernet buffer data heap.
  size_t iosize = 2ul * NET_BUFFERS * NET_BUFFERSZ;
  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(iosize, 0, &vmo); status != ZX_OK) {
    printf("netifc: create VMO failed: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  constexpr char kVmoName[] = "eth-buffers";
  if (zx_status_t status = vmo.set_property(ZX_PROP_NAME, kVmoName, sizeof(kVmoName));
      status != ZX_OK) {
    printf("netifc: set_property failed: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  fzl::VmoMapper mapper;
  if (zx_status_t status = mapper.Map(vmo, 0, iosize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      status != ZX_OK) {
    printf("netifc: map VMO failed: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }
  printf("netifc: create %zu eth buffers\n", eth_state.eth_buffer_base.size());
  // Assign data chunks to ethbufs.
  for (size_t n = 0; n < eth_state.eth_buffer_base.size(); n++) {
    eth_buffer& buffer = eth_state.eth_buffer_base[n];
    buffer = {
        .magic = ETH_BUFFER_MAGIC,
        .data = static_cast<uint8_t*>(mapper.start()) + (n * NET_BUFFERSZ),
        .state = ETH_BUFFER_FREE,
    };
    eth_state.PutBuffer(&buffer, ETH_BUFFER_FREE);
  }

  zx::result eth_status =
      EthClient::Create(dispatcher, std::move(device), std::move(vmo), mapper.start(),
                        fit::bind_member<&EthNetifc::HandleRx>(&eth_state),
                        fit::bind_member<&EthNetifc::HandleSignal>(&eth_state),
                        fit::bind_member<&EthNetifc::HandleClosed>(&eth_state));
  if (eth_status.is_error()) {
    printf("EthClient::create() failed: %s\n", eth_status.status_string());
    return eth_status.take_error();
  }
  std::unique_ptr<EthClient> eth = std::move(eth_status.value());

  // Enqueue rx buffers.
  for (unsigned n = 0; n < NET_BUFFERS; n++) {
    zx::result status = eth_state.GetBuffer(NET_BUFFERSZ, ETH_BUFFER_RX, false);
    if (status.is_error()) {
      printf("netifc: only queued %u buffers (desired: %u)\n", n, NET_BUFFERS);
      break;
    }
    eth_buffer* ethbuf = status.value();
    eth->QueueRx(ethbuf, ethbuf->data, NET_BUFFERSZ);
  }

  eth_state.iobuf = std::move(mapper);
  eth_state.eth = std::move(eth);

  g_state = std::move(state);
  return zx::ok();
}

zx::result<> open_netdevice(async_dispatcher_t* dispatcher, NetdeviceInterface iface,
                            fit::callback<void(zx_status_t)> on_error) {
  std::unique_ptr state = std::make_unique<NetdeviceIfc>(std::move(iface.device), dispatcher,
                                                         std::move(on_error), iface.port_id);
  NetdeviceIfc& ifc = *state;
  ifc.client.SetErrorCallback([&ifc](zx_status_t status) {
    printf("netsvc: netdevice error %s\n", zx_status_get_string(status));
    ifc.on_error(status);
  });
  ifc.client.SetRxCallback([dispatcher](network::client::NetworkDeviceClient::Buffer buffer) {
    ZX_ASSERT_MSG(buffer.data().parts() == 1, "received fragmented buffer with %d parts",
                  buffer.data().parts());
    cpp20::span data = buffer.data().part(0).data();
    netifc_recv(dispatcher, data.begin(), data.size());
  });
  ifc.client.OpenSession("netsvc", [&ifc](zx_status_t status) {
    if (status != ZX_OK) {
      printf("netsvc: netdevice failed to open session: %s\n", zx_status_get_string(status));
      ifc.on_error(status);
      return;
    }
    ifc.client.AttachPort(ifc.port_id, {fuchsia_hardware_network::wire::FrameType::kEthernet},
                          [&ifc](zx_status_t status) {
                            if (status != ZX_OK) {
                              printf("netsvc: failed to attach port: %s\n",
                                     zx_status_get_string(status));
                              ifc.on_error(status);
                              return;
                            }
                          });
  });
  g_state = std::move(state);
  return zx::ok();
}

zx::result<> netifc_open(async_dispatcher_t* dispatcher, cpp17::string_view interface,
                         fit::callback<void(zx_status_t)> on_error) {
  zx::result status = netifc_discover("/dev", interface);
  if (status.is_error()) {
    printf("netifc: failed to discover interface %s\n", status.status_string());
    return status.take_error();
  }
  auto& [dev, mac] = status.value();

  {
    zx::result status = std::visit(
        overloaded{[dispatcher, &on_error](
                       fidl::ClientEnd<fuchsia_hardware_ethernet::Device>& d) -> zx::result<> {
                     return open_ethernet(dispatcher, std::move(d), std::move(on_error));
                   },
                   [dispatcher, &on_error](NetdeviceInterface& d) -> zx::result<> {
                     return open_netdevice(dispatcher, std::move(d), std::move(on_error));
                   }},
        dev);
    if (status.is_error()) {
      return status.take_error();
    }
  }
  ip6_init(mac, false);
  return zx::ok();
}

void netifc_close() { g_state = nullptr; }
