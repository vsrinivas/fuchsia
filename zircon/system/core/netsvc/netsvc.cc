// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <zircon/boot/netboot.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/time.h>

#include "args.h"
#include "debuglog.h"
#include "netboot.h"
#include "tftp.h"

#define FILTER_IPV6 1

static bool g_netbootloader = false;

static char g_nodename[HOST_NAME_MAX];

bool netbootloader() { return g_netbootloader; }
const char* nodename() { return g_nodename; }

void udp6_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
  bool mcast = (memcmp(daddr, &ip6_ll_all_nodes, sizeof(ip6_addr_t)) == 0);

  switch (dport) {
    case NB_SERVER_PORT:
      netboot_recv(data, len, mcast, daddr, dport, saddr, sport);
      break;
    case DEBUGLOG_ACK_PORT:
      debuglog_recv(data, len, mcast);
      break;
    case NB_TFTP_INCOMING_PORT:
    case NB_TFTP_OUTGOING_PORT:
      tftp_recv(data, len, daddr, dport, saddr, sport);
      break;
  }
}

void netifc_recv(void* data, size_t len) { eth_recv(data, len); }

bool netifc_send_pending() {
  if (!tftp_has_pending()) {
    return false;
  }
  tftp_send_next();
  return tftp_has_pending();
}

void update_timeouts() {
  zx_time_t now = zx_clock_get_monotonic();
  zx_time_t next_timeout = (debuglog_next_timeout() < tftp_next_timeout()) ? debuglog_next_timeout()
                                                                           : tftp_next_timeout();
  if (next_timeout != ZX_TIME_INFINITE) {
    uint32_t ms = static_cast<uint32_t>(
        (next_timeout < now) ? 0 : (zx_time_sub_time(next_timeout, now)) / ZX_MSEC(1));
    netifc_set_timer(ms);
  }
}

static const char* zedboot_banner =
    "              _ _                 _   \n"
    "             | | |               | |  \n"
    "  _______  __| | |__   ___   ___ | |_ \n"
    " |_  / _ \\/ _` | '_ \\ / _ \\ / _ \\| __|\n"
    "  / /  __/ (_| | |_) | (_) | (_) | |_ \n"
    " /___\\___|\\__,_|_.__/ \\___/ \\___/ \\__|\n"
    "                                      \n"
    "\n";

int main(int argc, char** argv) {
  if (debuglog_init() < 0) {
    return -1;
  }

  const char* interface = NULL;
  bool should_advertise = false;

  const char* error;
  if (parse_netsvc_args(argc, argv, &error, &g_netbootloader, &should_advertise, &interface) < 0) {
    printf("netsvc: fatal error: %s\n", error);
    return -1;
  };

  gethostname(g_nodename, sizeof(g_nodename));

  if (interface != NULL) {
    printf("netsvc: looking for interface %s\n", interface);
  }

  for (;;) {
    if (netifc_open(interface) != 0) {
      printf("netsvc: fatal error initializing network\n");
      return -1;
    }

    if (g_netbootloader) {
      printf("%szedboot: version: %s\n\n", zedboot_banner, BOOTLOADER_VERSION);
    }

    printf("netsvc: nodename='%s'\n", g_nodename);
    if (!should_advertise) {
      printf("netsvc: will not advertise\n");
    }
    printf("netsvc: start\n");
    for (;;) {
      if (g_netbootloader && should_advertise) {
        netboot_advertise(g_nodename);
      }

      update_timeouts();

      if (netifc_poll()) {
        printf("netsvc: netifc_poll() failed - terminating\n");
        break;
      }
      zx_time_t now = zx_clock_get_monotonic();
      if (now > debuglog_next_timeout()) {
        debuglog_timeout_expired();
      }
      if (now > tftp_next_timeout()) {
        tftp_timeout_expired();
      }
    }
    netifc_close();
  }

  return 0;
}
