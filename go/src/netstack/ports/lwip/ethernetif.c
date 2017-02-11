// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include "third_party/lwip/src/include/lwip/opt.h"

#include "third_party/lwip/src/include/lwip/debug.h"
#include "third_party/lwip/src/include/lwip/def.h"
#include "third_party/lwip/src/include/lwip/ethip6.h"
#include "third_party/lwip/src/include/lwip/ip.h"
#include "third_party/lwip/src/include/lwip/mem.h"
#include "third_party/lwip/src/include/lwip/pbuf.h"
#include "third_party/lwip/src/include/lwip/snmp.h"
#include "third_party/lwip/src/include/lwip/stats.h"
#include "third_party/lwip/src/include/lwip/sys.h"
#include "third_party/lwip/src/include/lwip/timeouts.h"
#include "third_party/lwip/src/include/netif/etharp.h"

#include "apps/netstack/ports/lwip/ethernetif.h"

/* Define those to better describe your network interface. */
#define IFNAME0 'e'
#define IFNAME1 'n'

#define IO_TYPE_FD 1
#define IO_TYPE_CHANNEL 2

struct ethernetif {
  /* Add whatever per-interface state that is needed here. */
  int io_type;
  union {
    int fd;
    mx_handle_t h;
  } io;
};

/* Forward declarations. */
static void ethernetif_input(struct netif *netif);
static void ethernetif_thread(void *arg);

/*-----------------------------------------------------------------------------------*/
static int read_packet(struct netif *netif, void *buf, size_t count) {
  struct ethernetif *ethernetif = (struct ethernetif *)netif->state;

  if (ethernetif->io_type == IO_TYPE_FD) {
    return read(ethernetif->io.fd, buf, count);
  }
  if (ethernetif->io_type == IO_TYPE_CHANNEL) {
    uint32_t sz = count;
    mx_status_t r;
    if ((r = mx_channel_read(ethernetif->io.h, 0, buf, sz, &sz, NULL, 0,
                             NULL)) < 0)
      return -1;
    return sz;
  }
  printf("ethernetif: not initialized correctly\n");
  return ERR_INTERNAL;
}

static int write_packet(struct netif *netif, const void *buf, size_t count) {
  struct ethernetif *ethernetif = (struct ethernetif *)netif->state;

  if (ethernetif->io_type == IO_TYPE_FD) {
    return write(ethernetif->io.fd, buf, count);
  }
  if (ethernetif->io_type == IO_TYPE_CHANNEL) {
    ssize_t r;
    if ((r = mx_channel_write(ethernetif->io.h, 0, buf, count, NULL, 0)) < 0)
      return -1;
    return count;
  }
  printf("ethernetif: not initialized correctly\n");
  return -1;
}

static int open_ethernet_device(u8_t *hwaddr, u8_t *hwaddr_len) {
  DIR *dir;
  struct dirent *de;
  int fd = -1;

  if ((dir = opendir("/dev/class/ethernet")) == NULL) {
    printf("ethernetif_init: cannot open /dev/class/ethernet\n");
    return -1;
  }
  while ((de = readdir(dir)) != NULL) {
    if (de->d_name[0] == '.') {
      continue;
    }
    if ((fd = openat(dirfd(dir), de->d_name, O_RDWR)) >= 0) {
      break;
    }
  }
  LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: fd %d\n", fd));
  closedir(dir);

  if (*hwaddr_len < 6 || read(fd, hwaddr, 6) != 6) {
    printf("ethernetif_init: cannot read MAC address\n");
    close(fd);
    return -1;
  }
  LWIP_DEBUGF(
      NETIF_DEBUG,
      ("ethernetif_init: mac: %02x:%02x:%02x:%02x:%02x:%02x\n", hwaddr[0],
       hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]));
  *hwaddr_len = 6;

  return fd;
}

static void low_level_init(struct netif *netif) {
  struct ethernetif *ethernetif = (struct ethernetif *)netif->state;

  mx_handle_t h;
  int fd;

  netif->hwaddr_len = 6u;

  if ((h = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0))) !=
      MX_HANDLE_INVALID) {
    ethernetif->io_type = IO_TYPE_CHANNEL;
    ethernetif->io.h = h;
    printf("ethernetif_init: sending ipc signal\n");
    mx_status_t status = mx_object_signal_peer(h, 0, MX_USER_SIGNAL_0);
    if (status != NO_ERROR) {
      printf("ethernetif_init: could not signal handle peer! %d\n", status);
      return;
    }
    printf("ethernetif_init: using a startup handle\n");
  } else if ((fd = open_ethernet_device(netif->hwaddr, &netif->hwaddr_len)) >=
             0) {
    ethernetif->io_type = IO_TYPE_FD;
    ethernetif->io.fd = fd;
    printf("ethernetif_init: opened an ethernet devie\n");
  } else {
    printf("ethernetif_init: initialization failed\n");
    return;
  }

  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

  /* if the io type is fd, link up now */
  if (ethernetif->io_type == IO_TYPE_FD) netif_set_link_up(netif);

  sys_thread_new("ethernetif_thread", ethernetif_thread, netif,
                 DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
}

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
  char buf[1514]; /* MTU (1500) + header (14) */
  ssize_t written;

  pbuf_copy_partial(p, buf, p->tot_len, 0);

/*
 * Pad spaces toward the minimum packet size (60 bytes without FCS)
 * as the driver doesn't do it for us.
 */
#define MIN_WRITE_SIZE 60

  int write_len = p->tot_len;
  if (write_len < MIN_WRITE_SIZE) {
    memset(buf + write_len, 0, MIN_WRITE_SIZE - write_len);
    write_len = MIN_WRITE_SIZE;
  }
  written = write_packet(netif, buf, write_len);
  if (written == -1) {
    MIB2_STATS_NETIF_INC(netif, ifoutdiscards);
    printf("ethernetif: write %d bytes returned -1\n", p->tot_len);
  } else {
    MIB2_STATS_NETIF_ADD(netif, ifoutoctets, written);
  }
  return ERR_OK;
}

#define TIMER_MS(n) (((uint64_t)(n)) * 1000000ULL)

static struct pbuf *low_level_input(struct netif *netif) {
  struct pbuf *p;
  int len;
  /* TODO(toshik)
   * The driver expects at least 2048 bytes for the buffer size. Reading less
   * than that would fail (Currently 2048 is a magic number)
   */
  char buf[2048];

  len = read_packet(netif, buf, sizeof(buf));
  if (len < 0) {
    /* TODO(toshik)
     * Currently read() often fails because ethernetif_input() is called even
     * if the fd is not readable.
     */
    /* LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: read returned %d\n", len));
     */
    return NULL;
  }
  if (len == 8) {
    /* status message: mac (6 bytes) + mtu (2 bytes) */
    unsigned int mtu = *(unsigned short int *)(buf + 6);
    if (mtu > 0) {
      /* link up */
      memcpy(netif->hwaddr, buf, 6);
      printf(
          "ethernetif: link up (mac %02x:%02x:%02x:%02x:%02x:%02x,"
          " mtu %u)\n",
          (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3],
          (uint8_t)buf[4], (uint8_t)buf[5], mtu);
      netif_set_link_up(netif);
    } else {
      /* link down */
      printf("ethernetif: link down\n");
      netif_set_link_down(netif);
    }
  }

  MIB2_STATS_NETIF_ADD(netif, ifinoctets, len);

  /* We allocate a pbuf chain of pbufs from the pool. */
  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  if (p != NULL) {
    pbuf_take(p, buf, len);
    /* acknowledge that packet has been read(); */
  } else {
    /* drop packet(); */
    MIB2_STATS_NETIF_INC(netif, ifindiscards);
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: could not allocate pbuf\n"));
  }

  return p;
}

static void ethernetif_input(struct netif *netif) {
  struct pbuf *p = low_level_input(netif);

  if (p == NULL) {
/* TODO(toshik)
 * Currently low_level_input() may return NULL often because
 * ethernetif_input() is called even if the fd is not readable.
 * Disable the following code for now.
 */
#if 0
#if LINK_STATS
    LINK_STATS_INC(link.recv);
#endif /* LINK_STATS */
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: low_level_input returned NULL\n"));
#endif
    return;
  }

  if (netif->input(p, netif) != ERR_OK) {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: netif input error\n"));
    pbuf_free(p);
  }
}

err_t ethernetif_init(struct netif *netif) {
  struct ethernetif *ethernetif =
      (struct ethernetif *)mem_malloc(sizeof(struct ethernetif));

  if (ethernetif == NULL) {
    LWIP_DEBUGF(NETIF_DEBUG,
                ("ethernetif_init: out of memory for ethernetif\n"));
    return ERR_MEM;
  }
  ethernetif->io_type = -1;

  netif->state = ethernetif;
  MIB2_INIT_NETIF(netif, snmp_ifType_other, 100000000);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  netif->output = etharp_output;
  netif->output_ip6 = ethip6_output;
  netif->linkoutput = low_level_output;
  netif->mtu = 1500;

  low_level_init(netif);

  return ERR_OK;
}

/*-----------------------------------------------------------------------------------*/

static void ethernetif_thread(void *arg) {
  struct netif *netif;
  struct ethernetif *ethernetif;

  netif = (struct netif *)arg;
  ethernetif = (struct ethernetif *)netif->state;

  while (1) {
    if (ethernetif->io_type == IO_TYPE_FD) {
      mxio_wait_fd(ethernetif->io.fd, MXIO_EVT_READABLE, NULL,
                   MX_TIME_INFINITE);
    } else if (ethernetif->io_type == IO_TYPE_CHANNEL) {
      mx_status_t r;
      mx_signals_t pending;
      r = mx_object_wait_one(ethernetif->io.h,
                             MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                             MX_TIME_INFINITE, &pending);
      if (r < 0) {
        printf("ethernetif: handle wait error (%d)\n", r);
        return;
      }
      if (pending & MX_CHANNEL_PEER_CLOSED) {
        printf("ethernetif: handle closed\n");
        return;
      }
    }
    /* TODO(toshik) mxio_wait_fd() might return even if the fd is not readable.
     * we should check why it returned, but it is not possible as errno is not
     * set currently.
     */
    ethernetif_input(netif);
  }
}
