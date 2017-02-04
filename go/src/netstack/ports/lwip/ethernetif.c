// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/device/ethernet.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/watcher.h>

#include "third_party/lwip/src/include/lwip/debug.h"
#include "third_party/lwip/src/include/lwip/ethip6.h"
#include "third_party/lwip/src/include/lwip/mem.h"
#include "third_party/lwip/src/include/lwip/opt.h"
#include "third_party/lwip/src/include/lwip/pbuf.h"
#include "third_party/lwip/src/include/lwip/stats.h"
#include "third_party/lwip/src/include/netif/etharp.h"

#include "apps/netstack/trace.h"
#include "apps/netstack/ports/lwip/eth-client.h"
#include "apps/netstack/ports/lwip/ethernetif.h"

struct ethernetif {
  int netfd;
  eth_client_t* eth;
};

/*------------------------------------------------------------------*/
/* eth_buffer (based on magenta/system/ulib/inet6/netifc.c) */

#define NET_BUFFERS 64
#define NET_BUFFERSZ 2048

#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

#define ETH_BUFFER_FREE 0u    // on free list
#define ETH_BUFFER_TX 1u      // in tx ring
#define ETH_BUFFER_RX 2u      // in rx ring
#define ETH_BUFFER_CLIENT 3u  // in use by stack

struct eth_buffer {
  uint64_t magic;
  struct eth_buffer* next;
  void* data;
  uint32_t state;
  uint32_t reserved;
};

static_assert(sizeof(struct eth_buffer) == 32, "");

static struct eth_buffer* eth_buffer_base;
static size_t eth_buffer_count;

static int _check_ethbuf(struct eth_buffer* ethbuf, uint32_t state) {
  if (((uintptr_t)ethbuf) & 31) {
    error("ethbuf %p misaligned\n", ethbuf);
    return -1;
  }
  if ((ethbuf < eth_buffer_base) ||
      (ethbuf >= (eth_buffer_base + eth_buffer_count))) {
    error("ethbuf %p outside of arena\n", ethbuf);
    return -1;
  }
  if (ethbuf->magic != ETH_BUFFER_MAGIC) {
    error("ethbuf %p bad magic\n", ethbuf);
    return -1;
  }
  if (ethbuf->state != state) {
    error("ethbuf %p incorrect state (%u != %u)\n", ethbuf, ethbuf->state,
          state);
    return -1;
  }
  return 0;
}

static void check_ethbuf(struct eth_buffer* ethbuf, uint32_t state) {
  if (_check_ethbuf(ethbuf, state)) {
    __builtin_trap();
  }
}

static struct eth_buffer* eth_buffers[2] = {NULL, NULL};
#define TX 0
#define RX 1

static int eth_get_buffer(int direction,
                          size_t sz,
                          void** data,
                          struct eth_buffer** out,
                          uint32_t newstate) {
  struct eth_buffer* buf;
  if (sz > NET_BUFFERSZ) {
    return -1;
  }
  if (eth_buffers[direction] == NULL) {
    error("out of buffers for %s\n", direction == TX ? "TX" : "RX");
    return -1;
  }
  buf = eth_buffers[direction];
  eth_buffers[direction] = buf->next;
  buf->next = NULL;

  check_ethbuf(buf, ETH_BUFFER_FREE);

  buf->state = newstate;
  *data = buf->data;
  *out = buf;
  return 0;
}

static void eth_put_buffer(int direction,
                           struct eth_buffer* buf,
                           uint32_t state) {
  check_ethbuf(buf, state);
  buf->state = ETH_BUFFER_FREE;
  buf->next = eth_buffers[direction];
  eth_buffers[direction] = buf;
}

static mx_handle_t iovmo;
static void* iobuf;

static int eth_buffer_init(struct netif* netif) {
  struct ethernetif* ethernetif = (struct ethernetif*)netif->state;
  mx_status_t status;

  if (eth_buffer_base == NULL) {
    eth_buffer_base = memalign(sizeof(struct eth_buffer),
                               2 * NET_BUFFERS * sizeof(struct eth_buffer));
    if (eth_buffer_base == NULL) {
      goto fail_close_fd;
    }
    eth_buffer_count = 2 * NET_BUFFERS;
  }

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
    info("create %zu eth buffers\n", eth_buffer_count);
    // assign data chunks to ethbufs
    for (unsigned n = 0; n < eth_buffer_count; n++) {
      eth_buffer_base[n].magic = ETH_BUFFER_MAGIC;
      eth_buffer_base[n].data = iobuf + n * NET_BUFFERSZ;
      eth_buffer_base[n].state = ETH_BUFFER_FREE;
      eth_buffer_base[n].reserved = 0;
      eth_put_buffer(n / NET_BUFFERS, eth_buffer_base + n, ETH_BUFFER_FREE);
    }
  }

  status = eth_create(ethernetif->netfd, iovmo, iobuf, &ethernetif->eth);
  if (status < 0) {
    error("eth_create() failed: %d\n", status);
    goto fail_close_fd;
  }

  if ((status = ioctl_ethernet_start(ethernetif->netfd)) < 0) {
    error("ethernet_start(): %d\n", status);
    goto fail_destroy_client;
  }

  // enqueue rx buffers
  for (unsigned n = 0; n < NET_BUFFERS; n++) {
    void* data;
    struct eth_buffer* ethbuf;
    if (eth_get_buffer(RX, NET_BUFFERSZ, &data, &ethbuf, ETH_BUFFER_RX)) {
      error("only queued %u buffers (desired: %u)\n", n, NET_BUFFERS);
      break;
    }
    eth_queue_rx(ethernetif->eth, ethbuf, ethbuf->data, NET_BUFFERSZ, 0);
  }

  return 0;

fail_destroy_client:
  eth_destroy(ethernetif->eth);
  ethernetif->eth = NULL;
fail_close_fd:
  close(ethernetif->netfd);
  ethernetif->netfd = -1;
  return -1;
}

static void eth_buffer_deinit(struct netif* netif) {
  struct ethernetif* ethernetif = (struct ethernetif*)netif->state;
  if (ethernetif->netfd != -1) {
    close(ethernetif->netfd);
    ethernetif->netfd = -1;
  }
  if (ethernetif->eth != NULL) {
    eth_destroy(ethernetif->eth);
    ethernetif->eth = NULL;
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
        // was sitting in RX ioring. reclaim.
        eth_put_buffer(eth_buffer_base[n].state == ETH_BUFFER_TX ? TX : RX,
                       eth_buffer_base + n, eth_buffer_base[n].state);
        count++;
        break;
      default:
        error("ethbuf %p: illegal state %u\n", eth_buffer_base + n,
              eth_buffer_base[n].state);
        __builtin_trap();
        break;
    }
  }
  info("recovered %u buffers\n", count);
}

/*------------------------------------------------------------------*/

static void zero_padding(char* buf, int cur_len, int padded_len) {
  if (cur_len < padded_len) {
    memset(buf + cur_len, 0, padded_len - cur_len);
  }
}

static void tx_complete(void* ctx, void* cookie) {
  eth_put_buffer(TX, cookie, ETH_BUFFER_TX);
}

static err_t ethernetif_output(struct netif* netif, struct pbuf* p) {
  struct ethernetif* ethernetif = (struct ethernetif*)netif->state;

  /* pad toward the min packet size (60 bytes without FCS) */
  int len = p->tot_len < 60 ? 60 : p->tot_len;

  eth_complete_tx(ethernetif->eth, NULL, tx_complete);

  void* data;
  struct eth_buffer* ethbuf;
  if (eth_get_buffer(TX, len, &data, &ethbuf, ETH_BUFFER_CLIENT) == 0) {
    pbuf_copy_partial(p, data, p->tot_len, 0);
    zero_padding(data, p->tot_len, len);

    check_ethbuf(ethbuf, ETH_BUFFER_CLIENT);

    ethbuf->state = ETH_BUFFER_TX;
    mx_status_t status =
        eth_queue_tx(ethernetif->eth, ethbuf, ethbuf->data, len, 0);
    if (status < 0) {
      error("queue tx failed: %d\n", status);
      eth_put_buffer(TX, ethbuf, ETH_BUFFER_TX);
      return ERR_IF;
    }
  }

  return ERR_OK;
}

static void ethernetif_input(struct netif* netif, void* data, size_t len) {
  struct pbuf* p;

  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  if (p != NULL) {
    pbuf_take(p, data, len);
  } else {
    LWIP_DEBUGF(NETIF_DEBUG, ("could not allocate pbuf\n"));
  }

  if (p == NULL) {
#if LINK_STATS
    LINK_STATS_INC(link.recv);
#endif /* LINK_STATS */
    LWIP_DEBUGF(NETIF_DEBUG, ("low_level_input returned NULL\n"));
    return;
  }

  if (netif->input(p, netif) != ERR_OK) {
    LWIP_DEBUGF(NETIF_DEBUG, ("netif input error\n"));
    pbuf_free(p);
  }
}

static void rx_complete(void* ctx, void* cookie, size_t len, uint32_t flags) {
  struct netif* netif = (struct netif*)ctx;
  struct ethernetif* ethernetif = (struct ethernetif*)netif->state;
  struct eth_buffer* ethbuf = cookie;
  check_ethbuf(ethbuf, ETH_BUFFER_RX);
  ethernetif_input(netif, ethbuf->data, len);
  eth_queue_rx(ethernetif->eth, ethbuf, ethbuf->data, NET_BUFFERSZ, 0);
}

static int ethernetif_thread(void* arg) {
  struct netif* netif = (struct netif*)arg;
  struct ethernetif* ethernetif = (struct ethernetif*)netif->state;

  /* TODO: when to link down? */
  netif_set_link_up(netif);

  for (;;) {
    mx_status_t status;
    if ((status = eth_complete_rx(ethernetif->eth, netif, rx_complete)) < 0) {
      error("eth rx failed: %d\n", status);
      return -1;
    }
    status = eth_wait_rx(ethernetif->eth, MX_TIME_INFINITE);
    if ((status < 0) && (status != ERR_TIMED_OUT)) {
      error("eth rx wait failed: %d\n", status);
      return -1;
    }
  }
  return 0;
}

/*------------------------------------------------------------------*/

#define IFNAME0 'e'
#define IFNAME1 'n'

static mx_status_t ethernetif_init_cb(int dirfd, const char* fn, void* cookie) {
  struct netif* netif = (struct netif*)cookie;

  int netfd;
  if ((netfd = openat(dirfd, fn, O_RDWR)) < 0) {
    error("failed to open /dev/class/ethernet/%s\n", fn);
    return NO_ERROR;
  }
  info("/dev/class/ethernet/%s\n", fn);

  eth_info_t info;
  if (ioctl_ethernet_get_info(netfd, &info) < 0) {
    close(netfd);
    return NO_ERROR;
  }
  memcpy(netif->hwaddr, info.mac, 6);
  netif->hwaddr_len = 6u;

  info("mac %02x:%02x:%02x:%02x:%02x:%02x, mtu %zd\n",
       (uint8_t)netif->hwaddr[0], (uint8_t)netif->hwaddr[1],
       (uint8_t)netif->hwaddr[2], (uint8_t)netif->hwaddr[3],
       (uint8_t)netif->hwaddr[4], (uint8_t)netif->hwaddr[5], info.mtu);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  netif->output = etharp_output;
  netif->output_ip6 = ethip6_output;
  netif->linkoutput = ethernetif_output;
  netif->mtu = 1500; /* TODO: check info.mtu */
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

  struct ethernetif* ethernetif =
      (struct ethernetif*)mem_malloc(sizeof(struct ethernetif));
  if (ethernetif == NULL) {
    LWIP_DEBUGF(NETIF_DEBUG, ("out of memory for ethernetif\n"));
    return ERR_MEM;
  }
  netif->state = ethernetif;

  ethernetif->netfd = netfd;

  eth_buffer_init(netif);
  netif_set_remove_callback(netif, eth_buffer_deinit);

  thrd_t thread;
  if (thrd_create(&thread, ethernetif_thread, netif) != thrd_success) {
    error("failed to start ethernetif_thread\n");
  }

  // stop polling inside mxio_watch_directory()
  return 1;
}

err_t ethernetif_init(struct netif* netif) {
  int dirfd;
  if ((dirfd = open("/dev/class/ethernet", O_DIRECTORY | O_RDONLY)) < 0) {
    error("can't open /dev/class/ethernet (%d)\n", dirfd);
    return ERR_IF;
  }
  mx_status_t status = mxio_watch_directory(dirfd, ethernetif_init_cb, netif);
  if (status < 0) {
    error("failed to find ethernet device (%d)\n", status);
  }
  close(dirfd);

  return ERR_OK;
}
