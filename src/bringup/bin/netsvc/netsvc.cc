// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netsvc.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/netboot/netboot.h>
#include <lib/zx/clock.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

#include <fbl/unique_fd.h>

#include "src/bringup/bin/netsvc/args.h"
#include "src/bringup/bin/netsvc/debuglog.h"
#include "src/bringup/bin/netsvc/inet6.h"
#include "src/bringup/bin/netsvc/netboot.h"
#include "src/bringup/bin/netsvc/netifc.h"
#include "src/bringup/bin/netsvc/tftp.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

#ifndef ENABLE_SLAAC
// SLAAC RA's are disabled by default as they intefere with tests in environments where there are
// many devices on the same LAN.
#define ENABLE_SLAAC 0
#endif

static bool g_netbootloader = false;

// When false (default), will only respond to a limited number of commands.
// Currently, NB_QUERY (to support netls and netaddr), as well as responding to
// ICMP as usual.
static bool g_all_features = false;
static bool g_log_packets = false;

static char g_nodename[HOST_NAME_MAX];

bool netbootloader() { return g_netbootloader; }
bool all_features() { return g_all_features; }
const char* nodename() { return g_nodename; }

void udp6_recv(async_dispatcher_t* dispatcher, void* data, size_t len, const ip6_addr_t* daddr,
               uint16_t dport, const ip6_addr_t* saddr, uint16_t sport) {
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
      debuglog_recv(dispatcher, data, len, mcast);
      break;
    case NB_TFTP_INCOMING_PORT:
    case NB_TFTP_OUTGOING_PORT:
      tftp_recv(dispatcher, data, len, daddr, dport, saddr, sport);
      break;
  }
}

void netifc_recv(async_dispatcher_t* dispatcher, void* data, size_t len) {
  if (g_log_packets) {
    printf("netsvc: received %ld bytes: ", len);
    // Only print enough of a packet to help diagnose issues, we don't need the
    // full packet. The number was picked because it's the exact full length of
    // a neighbor solicitation accounting for Ethernet, IPv6, and ICMPv6 headers
    // plus the NS payload. It is also enough to fully observe a UDP packet's
    // headers, which is most of the transport-layer traffic.
    size_t print_len = std::min(len, static_cast<size_t>(86));
    for (const uint8_t& b : cpp20::span(reinterpret_cast<const uint8_t*>(data), print_len)) {
      printf("%02x", b);
    }
    if (print_len != len) {
      printf("...");
    }
    printf("\n");
  }
  eth_recv(dispatcher, data, len);
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
  if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);
    printf("Failed to redirect stdout to debuglog, assuming test environment and continuing: %s\n",
           zx_status_get_string(status));
  }

  fbl::unique_fd svc_root(open("/svc", O_RDWR | O_DIRECTORY));
  fdio_cpp::UnownedFdioCaller caller(svc_root.get());

  NetsvcArgs args;
  const char* error;
  if (ParseArgs(argc, argv, caller.directory(), &error, &args) < 0) {
    printf("netsvc: fatal error: %s\n", error);
    return -1;
  };
  if (args.disable) {
    printf("netsvc: Disabled. Exiting.\n");
    return 0;
  }

  bool print_nodename_and_exit = args.print_nodename_and_exit;
  bool should_advertise = args.advertise;
  g_netbootloader = args.netboot;
  g_all_features = args.all_features;
  g_log_packets = args.log_packets;
  const std::string& interface = args.interface;

  if (g_netbootloader && !g_all_features) {
    printf("netsvc: fatal: --netboot requires --all-features\n");
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  if (g_all_features) {
    if (zx_status_t status = debuglog_init(loop.dispatcher()); status != ZX_OK) {
      printf("netsvc: fatal: error initializing debuglog: %s\n", zx_status_get_string(status));
      return -1;
    }
  }

  printf("netsvc: running in %s mode\n", g_all_features ? "full" : "limited");

  if (gethostname(g_nodename, sizeof(g_nodename)) != 0) {
    printf("netsvc: gethostname failed: %s\n", strerror(errno));
    return -1;
  }

  printf("netsvc: nodename='%s'\n", g_nodename);

  if (print_nodename_and_exit) {
    return 0;
  }

  if (!interface.empty()) {
    printf("netsvc: looking for interface %s\n", interface.c_str());
  }

  if (!should_advertise) {
    printf("netsvc: will not advertise\n");
  }

  if (g_netbootloader) {
    printf("%szedboot: version: %s\n\n", zedboot_banner, BOOTLOADER_VERSION);
  }

  for (;;) {
    if (zx::result status = netifc_open(loop.dispatcher(), interface,
                                        [&loop](zx_status_t error) {
                                          printf("netsvc: interface error: %s\n",
                                                 zx_status_get_string(error));
                                          loop.Quit();
                                        });
        status.is_error()) {
      printf("netsvc: fatal: failed to open interface: %s\n", status.status_string());
      return -1;
    }

    printf("netsvc: start\n");

    async::Task advertise_task(
        [](async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
          if (status == ZX_ERR_CANCELED) {
            return;
          }
          ZX_ASSERT_MSG(status == ZX_OK, "advertise task dispatch failed %s",
                        zx_status_get_string(status));
#if ENABLE_SLAAC
          send_router_advertisement();
#endif
          netboot_advertise(g_nodename);

          status = task->PostDelayed(dispatcher, zx::sec(1));
          ZX_ASSERT_MSG(status == ZX_OK, "failed to post advertise task %s",
                        zx_status_get_string(status));
        });

    if (g_netbootloader && should_advertise) {
      if (zx_status_t status = advertise_task.Post(loop.dispatcher()); status != ZX_OK) {
        printf("netsvc: fatal: failed to post advertise task: %s\n", zx_status_get_string(status));
        return -1;
      }
    }

    loop.Run();
    if (zx_status_t status = advertise_task.Cancel();
        status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
      printf("netsvc: fatal: failed to cancel advertise task: %s\n", zx_status_get_string(status));
      return -1;
    }
    netifc_close();
    // Something went wrong bringing up the interface, reset the loop and go
    // again in a bit.
    {
      zx_status_t status = loop.ResetQuit();
      ZX_ASSERT_MSG(status == ZX_OK, "failed to reset loop: %s", zx_status_get_string(status));
      printf("netsvc: waiting before retrying...\n");
      sleep(1);
    }
  }
}
