// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header is used only by platgen.sh to create the raw rust bindings.

#ifndef SRC_CONNECTIVITY_LOWPAN_LIB_OPENTHREAD_SYS_PLATGEN_H_
#define SRC_CONNECTIVITY_LOWPAN_LIB_OPENTHREAD_SYS_PLATGEN_H_

#define OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE 1

#include <openthread/backbone_router.h>
#include <openthread/backbone_router_ftd.h>
#include <openthread/border_agent.h>
#include <openthread/border_router.h>
#include <openthread/channel_manager.h>
#include <openthread/channel_monitor.h>
#include <openthread/child_supervision.h>
#include <openthread/cli.h>
#include <openthread/coap.h>
#include <openthread/coap_secure.h>
#include <openthread/commissioner.h>
#include <openthread/config.h>
#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/dataset_updater.h>
#include <openthread/diag.h>
#include <openthread/dns.h>
#include <openthread/dns_client.h>
#include <openthread/dnssd_server.h>
#include <openthread/error.h>
#include <openthread/heap.h>
#include <openthread/icmp6.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/jam_detection.h>
#include <openthread/joiner.h>
#include <openthread/link.h>
#include <openthread/link_raw.h>
#include <openthread/logging.h>
#include <openthread/message.h>
#include <openthread/multi_radio.h>
#include <openthread/ncp.h>
#include <openthread/netdata.h>
#include <openthread/netdiag.h>
#include <openthread/network_time.h>
#include <openthread/ping_sender.h>
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/debug_uart.h>
#include <openthread/platform/diag.h>
#include <openthread/platform/entropy.h>
#include <openthread/platform/flash.h>
#include <openthread/platform/infra_if.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/memory.h>
#include <openthread/platform/messagepool.h>
#include <openthread/platform/misc.h>
#include <openthread/platform/otns.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/settings.h>
#include <openthread/platform/spi-slave.h>
#include <openthread/platform/time.h>
#include <openthread/platform/toolchain.h>
#include <openthread/platform/trel.h>
#include <openthread/platform/udp.h>
#include <openthread/random_noncrypto.h>
#include <openthread/server.h>
#include <openthread/sntp.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>
#include <openthread/srp_server.h>
#include <openthread/tasklet.h>
#include <openthread/tcp.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <openthread/trel.h>
#include <openthread/udp.h>

#endif  // SRC_CONNECTIVITY_LOWPAN_LIB_OPENTHREAD_SYS_PLATGEN_H_
