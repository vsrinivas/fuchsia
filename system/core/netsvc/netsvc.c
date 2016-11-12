// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <threads.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <mxio/util.h>
#include <launchpad/launchpad.h>

#include <magenta/netboot.h>

#define FILTER_IPV6 1

#define MAX_LOG_LINE (MX_LOG_RECORD_MAX + 32)

static mx_handle_t loghandle;

int get_log_line(char* out) {
    char buf[MX_LOG_RECORD_MAX + 1];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    if (mx_log_read(loghandle, MX_LOG_RECORD_MAX, rec, 0) > 0) {
        if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
            rec->datalen--;
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

#define MAX_LOG_DATA 1280

typedef struct logpacket {
    uint32_t magic;
    uint32_t seqno;
    char data[MAX_LOG_DATA];
} logpacket_t;

static volatile uint32_t seqno = 1;
static volatile uint32_t pending = 0;

static void run_program(const char *progname, int argc, const char** argv, mx_handle_t h) {
    mx_handle_t handles[4];
    uint32_t ids[4];

    if (mxio_clone_root(handles, ids) < 0) {
        return;
    }
    if ((handles[1] = mx_log_create(0)) < 0) {
        mx_handle_close(handles[0]);
        return;
    }
    if ((handles[2] = mx_log_create(0)) < 0) {
        mx_handle_close(handles[0]);
        mx_handle_close(handles[1]);
        return;
    }
    handles[3] = h;
    ids[1] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 1);
    ids[2] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 2);
    ids[3] = MX_HND_INFO(MX_HND_TYPE_USER0, 0);

    mx_handle_t proc;
    if ((proc = launchpad_launch_mxio_etc(progname, argc, argv,
                                          (const char* const*) environ,
                                          (h != 0) ? 4 : 3, handles, ids)) < 0) {
        printf("netsvc: cannot launch %s\n", argv[0]);
    }
}

static void run_command(const char* cmd) {
    const char* args[] = {
        "/boot/bin/mxsh", "-c", cmd
    };
    printf("net cmd: %s\n", cmd);
    run_program("net:mxsh", 3, args, 0);
}

static void run_server(const char* progname, const char* bin, mx_handle_t h) {
    run_program(progname, 1, &bin, h);
}

static const char* hostname = "magenta";

void udp6_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {

    bool mcast = (memcmp(daddr, &ip6_ll_all_nodes, sizeof(ip6_addr_t)) == 0);

    if (dport == NB_SERVER_PORT) {
        nbmsg* msg = data;
        if ((len < (sizeof(nbmsg) + 1)) ||
            (msg->magic != NB_MAGIC)) {
            return;
        }
        // null terminate the payload
        len -= sizeof(nbmsg);
        msg->data[len - 1] = 0;

        switch (msg->cmd) {
        case NB_QUERY:
            if (strcmp((char*)msg->data, "*") &&
                strcmp((char*)msg->data, hostname)) {
                break;
            }
            size_t dlen = strlen(hostname) + 1;
            char buf[1024 + sizeof(nbmsg)];
            if ((dlen + sizeof(nbmsg)) > sizeof(buf)) {
                return;
            }
            msg->cmd = NB_ACK;
            memcpy(buf, msg, sizeof(nbmsg));
            memcpy(buf + sizeof(nbmsg), hostname, dlen);
            udp6_send(buf, sizeof(nbmsg) + dlen, saddr, sport, dport);
            break;
        case NB_SHELL_CMD:
            if (!mcast) {
                run_command((char*) msg->data);
                return;
            }
            break;
        case NB_OPEN:
            netfile_open((char*)msg->data, msg->cookie, msg->arg, saddr, sport, dport);
            break;
        case NB_READ:
            netfile_read(msg->cookie, msg->arg, saddr, sport, dport);
            break;
        case NB_WRITE:
            len--; // NB NUL-terminator is not part of the data
            netfile_write((char*)msg->data, len, msg->cookie, msg->arg, saddr, sport, dport);
            break;
        case NB_CLOSE:
            netfile_close(msg->cookie, saddr, sport, dport);
            break;
        }
        return;
    }

    if (dport == DEBUGLOG_ACK_PORT) {
        if ((len != 8) || mcast) {
            return;
        }
        logpacket_t* pkt = data;
        if ((pkt->magic != 0xaeae1123) || (pkt->seqno != seqno)) {
            return;
        }
        if (pending) {
            seqno++;
            pending = 0;
            // ensure we stop polling
            netifc_set_timer(0);
        }
    }
}

mx_handle_t ipc_handle;

static int ipc_thread(void* arg) {
    uint8_t data[2048];

    for (;;) {
        mx_status_t status;
        uint32_t n = sizeof(data);
        if ((status = mx_channel_read(ipc_handle, 0, data, n, &n, NULL, 0, NULL)) < 0) {
            if (status == ERR_SHOULD_WAIT) {
                mx_handle_wait_one(ipc_handle, MX_SIGNAL_READABLE, MX_TIME_INFINITE, NULL);
                continue;
            }
            printf("netsvc: ipc read failed: %d\n", status);
            break;
        }
#if FILTER_IPV6
        if ((n >= ETH_HDR_LEN) &&
            (*((uint16_t*) (data + 12)) == ETH_IP6)) {
            continue;
        }
#endif
        netifc_send(data, n);
    }
    mx_handle_close(ipc_handle);
    ipc_handle = 0;
    return 0;
}

void netifc_recv(void* data, size_t len) {
    eth_recv(data, len);

    if (ipc_handle) {
#if FILTER_IPV6
        if ((len >= ETH_HDR_LEN) &&
            (*((uint16_t*) (data + 12)) == ETH_IP6)) {
            return;
        }
#endif
        mx_channel_write(ipc_handle, 0, data, len, NULL, 0);
    }
}

int main(int argc, char** argv) {
    logpacket_t pkt;
    int len = 0;
    if ((loghandle = mx_log_create(MX_LOG_FLAG_READABLE)) < 0) {
        return -1;
    }

    printf("netsvc: main()\n");

    mx_handle_t h[2] = { 0, 0 };
    if (mx_channel_create(0, &h[0], &h[1]) == NO_ERROR) {
        run_server("netstack", "/system/bin/netstack", h[1]);
        ipc_handle = h[0];
        thrd_t t;
        if (thrd_create(&t, ipc_thread, h) != thrd_success) {
            mx_handle_close(ipc_handle);
            ipc_handle = 0;
        }
    }
    for (;;) {
        if (netifc_open() != 0) {
            printf("netsvc: fatal error initializing network\n");
            return -1;
        }

        if (ipc_handle) {
            uint8_t info[8];
            netifc_get_info(info, (uint16_t*) (info + 6));
            mx_channel_write(ipc_handle, 0, info, 8, NULL, 0);
        }

        printf("netsvc: start\n");
        for (;;) {
            if (pending == 0) {
                pkt.magic = 0xaeae1123;
                pkt.seqno = seqno;
                len = 0;
                while (len < (MAX_LOG_DATA - MAX_LOG_LINE)) {
                    int r = get_log_line(pkt.data + len);
                    if (r > 0) {
                        len += r;
                    } else {
                        break;
                    }
                }
                if (len) {
                    len += 8;
                    pending = 1;
                    goto transmit;
                }
            }
            if (netifc_timer_expired()) {
            transmit:
                if (pending) {
                    udp6_send(&pkt, 8 + len, &ip6_ll_all_nodes, DEBUGLOG_PORT, DEBUGLOG_ACK_PORT);
                }
            }
            //TODO: wakeup early for log traffic too
            netifc_set_timer(100);
            if (netifc_poll())
                break;
        }
        netifc_close();

        if (ipc_handle) {
            uint8_t info[8] = { 0, };
            mx_channel_write(ipc_handle, 0, info, 8, NULL, 0);
        }
    }

    return 0;
}
