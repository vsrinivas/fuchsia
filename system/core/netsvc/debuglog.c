// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include "netsvc.h"

#define MAX_LOG_LINE (ZX_LOG_RECORD_MAX + 32)

static zx_handle_t loghandle;
static logpacket_t pkt;
static int pkt_len;

static volatile uint32_t seqno = 1;
static volatile uint32_t pending = 0;

zx_time_t debuglog_next_timeout = ZX_TIME_INFINITE;

#define SEND_DELAY_SHORT ZX_MSEC(100)
#define SEND_DELAY_LONG ZX_SEC(4)

// Number of consecutive unacknowledged packets we will send before reducing send rate.
static const unsigned kUnackedThreshold = 5;

// Number of consecutive packets that went unacknowledged. Is reset on acknowledgment.
static unsigned num_unacked = 0;

// How long to wait between sending.
static zx_duration_t send_delay = SEND_DELAY_SHORT;

static int get_log_line(char* out) {
    char buf[ZX_LOG_RECORD_MAX + 1];
    zx_log_record_t* rec = (zx_log_record_t*)buf;
    for (;;) {
        if (zx_log_read(loghandle, ZX_LOG_RECORD_MAX, rec, 0) > 0) {
            if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
                rec->datalen--;
            }
            // records flagged for local display are ignored
            if (rec->flags & ZX_LOG_LOCAL) {
                continue;
            }
            rec->data[rec->datalen] = 0;
            snprintf(out, MAX_LOG_LINE, "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
                     (int)(rec->timestamp / 1000000000ULL),
                     (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                     rec->pid, rec->tid, rec->data);
            return strlen(out);
        } else {
            return 0;
        }
    }
}

int debuglog_init(void) {
    if (zx_log_create(ZX_LOG_FLAG_READABLE, &loghandle) < 0) {
        return -1;
    }

    // Set up our timeout to expire immediately, so that we check for pending log messages
    debuglog_next_timeout = zx_clock_get(ZX_CLOCK_MONOTONIC);

    seqno = 1;
    pending = 0;

    return 0;
}

// If we have an outstanding (unacknowledged) log, resend it. Otherwise, send new logs, if we
// have any.
static void debuglog_send(void) {
    if (pending == 0) {
        pkt.magic = NB_DEBUGLOG_MAGIC;
        pkt.seqno = seqno;
        strncpy(pkt.nodename, nodename, sizeof(pkt.nodename) - 1);
        pkt_len = 0;
        while (pkt_len < (MAX_LOG_DATA - MAX_LOG_LINE)) {
            int r = get_log_line(pkt.data + pkt_len);
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
    debuglog_next_timeout = zx_deadline_after(send_delay);
}

void debuglog_recv(void* data, size_t len, bool is_mcast) {
    // The only message we should be receiving is acknowledgement of our last transmission
    if (!pending) {
        return;
    }
    if ((len != 8) || is_mcast) {
        return;
    }
    logpacket_t* pkt = data;
    if ((pkt->magic != NB_DEBUGLOG_MAGIC) || (pkt->seqno != seqno)) {
        return;
    }

    // Received an ack. We have an active listener. Don't delay.
    num_unacked = 0;
    send_delay = SEND_DELAY_SHORT;

    seqno++;
    pending = 0;
    debuglog_send();
}

void debuglog_timeout_expired(void) {
    if (pending) {
        // No reply. If noone is listening, reduce send rate.
        if (++num_unacked >= kUnackedThreshold) {
            send_delay = SEND_DELAY_LONG;
        }
    }
    debuglog_send();
}

