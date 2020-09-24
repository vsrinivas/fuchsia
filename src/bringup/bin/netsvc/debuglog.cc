// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debuglog.h"

#include <inttypes.h>
#include <lib/zx/clock.h>
#include <lib/zx/debuglog.h>
#include <stdio.h>
#include <string.h>
#include <zircon/boot/netboot.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include "netsvc.h"
#include "tftp.h"

#define MAX_LOG_LINE (ZX_LOG_RECORD_MAX + 32)

static zx::debuglog debuglog;
static logpacket_t pkt;
static size_t pkt_len;

static volatile uint32_t seqno = 1;
static volatile uint32_t pending = 0;

static zx_time_t g_debuglog_next_timeout = ZX_TIME_INFINITE;

zx_time_t debuglog_next_timeout() { return g_debuglog_next_timeout; }

#define SEND_DELAY_SHORT ZX_MSEC(100)
#define SEND_DELAY_LONG ZX_SEC(4)

// Number of consecutive unacknowledged packets we will send before reducing send rate.
static const unsigned kUnackedThreshold = 5;

// Number of consecutive packets that went unacknowledged. Is reset on acknowledgment.
static unsigned num_unacked = 0;

// How long to wait between sending.
static zx_duration_t send_delay = SEND_DELAY_SHORT;

static size_t get_log_line(char* out) {
  char buf[ZX_LOG_RECORD_MAX + 1];
  zx_log_record_t* rec = reinterpret_cast<zx_log_record_t*>(buf);
  for (;;) {
    if (debuglog.read(0, rec, ZX_LOG_RECORD_MAX) > 0) {
      if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
        rec->datalen--;
      }
      // records flagged for local display are ignored
      if (rec->flags & ZX_LOG_LOCAL) {
        continue;
      }
      rec->data[rec->datalen] = 0;
      snprintf(out, MAX_LOG_LINE, "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
               static_cast<int>(rec->timestamp / 1000000000ULL),
               static_cast<int>((rec->timestamp / 1000000ULL) % 1000ULL), rec->pid, rec->tid,
               rec->data);
      return strlen(out);
    } else {
      return 0;
    }
  }
}

int debuglog_init() {
  if (zx::debuglog::create(zx::resource(), ZX_LOG_FLAG_READABLE, &debuglog) < 0) {
    return -1;
  }

  // Set up our timeout to expire immediately, so that we check for pending log messages
  g_debuglog_next_timeout = zx::clock::get_monotonic().get();

  seqno = 1;
  pending = 0;

  return 0;
}

// If we have an outstanding (unacknowledged) log, resend it. Otherwise, send new logs, if we
// have any.
static void debuglog_send() {
  if (pending == 0) {
    pkt.magic = NB_DEBUGLOG_MAGIC;
    pkt.seqno = seqno;
    strncpy(pkt.nodename, nodename(), sizeof(pkt.nodename) - 1);
    pkt_len = 0;
    while (pkt_len < (MAX_LOG_DATA - MAX_LOG_LINE)) {
      size_t r = get_log_line(pkt.data + pkt_len);
      if (r > 0) {
        pkt_len += r;
      } else {
        break;
      }
    }
    if (pkt_len) {
      // include header and nodename in length
      pkt_len += MAX_NODENAME_LENGTH + sizeof(uint32_t) * 2;
      pending = 1;
    } else {
      goto done;
    }
  }
  udp6_send(&pkt, pkt_len, &ip6_ll_all_nodes, DEBUGLOG_PORT, DEBUGLOG_ACK_PORT, false);
done:
  g_debuglog_next_timeout = zx_deadline_after(send_delay);
}

void debuglog_recv(void* data, size_t len, bool is_mcast) {
  // The only message we should be receiving is acknowledgement of our last transmission
  if (!pending) {
    return;
  }
  if ((len != 8) || is_mcast) {
    return;
  }
  // Copied not cast in-place to satisfy alignment requirements flagged by ubsan (see fxbug.dev/45798).
  logpacket_t pkt;
  memcpy(&pkt, data, sizeof(logpacket_t));
  if ((pkt.magic != NB_DEBUGLOG_MAGIC) || (pkt.seqno != seqno)) {
    return;
  }

  // Received an ack. We have an active listener. Don't delay.
  num_unacked = 0;
  send_delay = SEND_DELAY_SHORT;

  seqno++;
  pending = 0;
  debuglog_send();
}

void debuglog_timeout_expired() {
  if (pending) {
    // No reply. If noone is listening, reduce send rate.
    if (++num_unacked >= kUnackedThreshold) {
      send_delay = SEND_DELAY_LONG;
    }
  }
  debuglog_send();
}
