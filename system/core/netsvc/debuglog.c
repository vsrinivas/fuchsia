// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

#include "netsvc.h"

#define MAX_LOG_LINE (MX_LOG_RECORD_MAX + 32)

static mx_handle_t loghandle;
static logpacket_t pkt;
static int pkt_len;

static volatile uint32_t seqno = 1;
static volatile uint32_t pending = 0;

mx_time_t debuglog_next_timeout = MX_TIME_INFINITE;

static int get_log_line(char* out) {
    char buf[MX_LOG_RECORD_MAX + 1];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    for (;;) {
        if (mx_log_read(loghandle, MX_LOG_RECORD_MAX, rec, 0) > 0) {
            if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
                rec->datalen--;
            }
            // records flagged for local display are ignored
            if (rec->flags & MX_LOG_LOCAL) {
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
    if (mx_log_create(MX_LOG_FLAG_READABLE, &loghandle) < 0) {
        return -1;
    }

    // Set up our timeout to expire immediately, so that we check for pending log messages
    debuglog_next_timeout = mx_time_get(MX_CLOCK_MONOTONIC);

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
    udp6_send(&pkt, pkt_len, &ip6_ll_all_nodes, DEBUGLOG_PORT, DEBUGLOG_ACK_PORT);
done:
    debuglog_next_timeout = mx_deadline_after(MX_MSEC(100));
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

    seqno++;
    pending = 0;
    debuglog_send();
}

void debuglog_timeout_expired(void) {
    debuglog_send();
}

