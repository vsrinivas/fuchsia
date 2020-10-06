// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <fcntl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/clock.h>
#include <unistd.h>
#include <zircon/boot/netboot.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

#include <fbl/unique_fd.h>
#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include "args.h"
#include "debuglog.h"
#include "netboot.h"
#include "tftp.h"

#ifndef ENABLE_SLAAC
#define ENABLE_SLAAC 1
#endif

static bool g_netbootloader = false;

// When false (default), will only respond to a limited number of commands.
// Currently, NB_QUERY (to support netls and netaddr), as well as responding to
// ICMP as usual.
static bool g_all_features = false;

static char g_nodename[HOST_NAME_MAX];

bool netbootloader() { return g_netbootloader; }
bool all_features() { return g_all_features; }
const char* nodename() { return g_nodename; }

void udp6_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
  bool mcast = (memcmp(daddr, &ip6_ll_all_nodes, sizeof(ip6_addr_t)) == 0);

  if (!all_features()) {
    if (dport != NB_SERVER_PORT) {
      // Only some netboot commands allowed in limited mode.
      return;
    }
  }

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
  fbl::unique_fd svc_root(open("/svc", O_RDWR | O_DIRECTORY));
  fdio_cpp::UnownedFdioCaller caller(svc_root.get());

  NetsvcArgs args;
  const char* error;
  if (ParseArgs(argc, argv, *caller.channel(), &error, &args) < 0) {
    printf("netsvc: fatal error: %s\n", error);
    return -1;
  };
  if (args.disable) {
    printf("netsvc: Disabled. Exiting.\n");
    return 0;
  }

  bool print_nodename_and_exit = args.nodename;
  bool should_advertise = args.advertise;
  g_netbootloader = args.netboot;
  g_all_features = args.all_features;
  const char* interface = args.interface.empty() ? nullptr : args.interface.c_str();

  if (g_netbootloader && !g_all_features) {
    printf("netsvc: fatal: --netboot requires --all-features\n");
    return -1;
  }

  if (g_all_features) {
    if (debuglog_init() < 0) {
      return -1;
    }
  }

  printf("netsvc: running in %s mode\n", g_all_features ? "full" : "limited");

  gethostname(g_nodename, sizeof(g_nodename));

  printf("netsvc: nodename='%s'\n", g_nodename);

  if (print_nodename_and_exit) {
    return 0;
  }

  if (interface != nullptr) {
    printf("netsvc: looking for interface %s\n", interface);
  }

  if (!should_advertise) {
    printf("netsvc: will not advertise\n");
  }

  for (;;) {
    if (netifc_open(interface) != 0) {
      printf("netsvc: fatal error initializing network\n");
      return -1;
    }

    if (g_netbootloader) {
      printf("%szedboot: version: %s\n\n", zedboot_banner, BOOTLOADER_VERSION);
    }

    printf("netsvc: start\n");

    zx_time_t advertise_next_timeout = ZX_TIME_INFINITE;
    if (g_netbootloader && should_advertise) {
      advertise_next_timeout = zx::clock::get_monotonic().get();
    }

    for (;;) {
      if (netifc_poll(
              std::min({advertise_next_timeout, debuglog_next_timeout(), tftp_next_timeout()}))) {
        printf("netsvc: netifc_poll() failed - terminating\n");
        break;
      }

      zx_time_t now = zx::clock::get_monotonic().get();
      if (now > advertise_next_timeout) {
#if ENABLE_SLAAC
        send_router_advertisement();
#endif
        netboot_advertise(g_nodename);
        advertise_next_timeout = zx_deadline_after(ZX_SEC(1));
      }
      if (now > debuglog_next_timeout()) {
        debuglog_timeout_expired();
      }
      if (now > tftp_next_timeout()) {
        tftp_timeout_expired();
      }
    }
    netifc_close();
  }
}
