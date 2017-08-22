// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <mxio/io.h>

#include <magenta/boot/netboot.h>

#include "device_id.h"

#define FILTER_IPV6 1

bool netbootloader = false;

static void run_program(const char *progname, int argc, const char** argv, mx_handle_t h) {

    launchpad_t* lp;
    launchpad_create(0, progname, &lp);
    launchpad_clone(lp, LP_CLONE_ALL & (~LP_CLONE_MXIO_STDIO));
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    mx_handle_t handle = MX_HANDLE_INVALID;
    mx_log_create(0, &handle);
    launchpad_add_handle(lp, handle, PA_HND(PA_MXIO_LOGGER, 0 | MXIO_FLAG_USE_FOR_STDIO));
    if (h != MX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_HND(PA_USER0, 0));
    }
    mx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) < 0) {
        printf("netsvc: cannot launch %s: %d: %s\n", argv[0], status, errmsg);
    }
}

void netboot_run_cmd(const char* cmd) {
    const char* args[] = {
        "/boot/bin/sh", "-c", cmd
    };
    printf("net cmd: %s\n", cmd);
    run_program("net:sh", 3, args, 0);
}

static void run_server(const char* progname, const char* bin, mx_handle_t h) {
    run_program(progname, 1, &bin, h);
}

const char* nodename = "magenta";

void udp6_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
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

void netifc_recv(void* data, size_t len) {
    eth_recv(data, len);
}

static const char* mxboot_banner =
"\n"
"                                  _          _                 _   \n"
" _ __ ___   __ _  __ _  ___ _ __ | |_ __ _  | |__   ___   ___ | |_ \n"
"| '_ ` _ \\ / _` |/ _` |/ _ \\ '_ \\| __/ _` | | '_ \\ / _ \\ / _ \\| __|\n"
"| | | | | | (_| | (_| |  __/ | | | || (_| | | |_) | (_) | (_) | |_ \n"
"|_| |_| |_|\\__,_|\\__, |\\___|_| |_|\\__\\__,_| |_.__/ \\___/ \\___/ \\__|\n"
"                 |___/                                             \n"
"\n";

int main(int argc, char** argv) {
    unsigned char mac[6];
    uint16_t mtu;
    char device_id[DEVICE_ID_MAX];

    if (debuglog_init() < 0) {
        return -1;
    }

    bool nodename_provided = false;
    while (argc > 1) {
        if (!strncmp(argv[1], "--netboot", 9)) {
            netbootloader = true;
        } else {
            nodename = argv[1];
            nodename_provided = true;
        }
        argv++;
        argc--;
    }

    for (;;) {
        if (netifc_open() != 0) {
            printf("netsvc: fatal error initializing network\n");
            return -1;
        }

        // Use mac address to generate unique nodename unless one was provided.
        if (!nodename_provided) {
            netifc_get_info(mac, &mtu);
            device_id_get(mac, device_id);
            nodename = device_id;
        }

        if (netbootloader) {
            puts(mxboot_banner);
        }

        printf("netsvc: nodename='%s'\n", nodename);
        printf("netsvc: start\n");
        for (;;) {
            if (netbootloader)
                netboot_advertise(nodename);

            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
            mx_time_t next_timeout = (debuglog_next_timeout < tftp_next_timeout) ?
                                     debuglog_next_timeout : tftp_next_timeout;
            if (next_timeout != MX_TIME_INFINITE) {
                netifc_set_timer((next_timeout < now) ? 0 :
                                 ((next_timeout - now)/MX_MSEC(1)));
            }
            if (netifc_poll())
                break;
            now = mx_time_get(MX_CLOCK_MONOTONIC);
            if (now > debuglog_next_timeout) {
                debuglog_timeout_expired();
            }
            if (now > tftp_next_timeout) {
                tftp_timeout_expired();
            }
        }
        netifc_close();
    }

    return 0;
}
