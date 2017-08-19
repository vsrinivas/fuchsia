// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "netprotocol.h"

static const char* hostname;
static struct sockaddr_in6 addr;
static bool found = false;
static char found_device_nodename[MAX_NODENAME_LENGTH];
static bool fuchsia_address = false;
static const char* appname;

static bool on_device(device_info_t* device, void* cookie) {
    if (hostname != NULL && strcmp(hostname, device->nodename)) {
        // Asking for a specific address and this isn't it.
        return true;
    }

    if (found && strcmp(found_device_nodename, device->nodename) != 0) {
        fprintf(stderr, "Multiple devices found, including %s and %s. Specify a hostname.\n",
                found_device_nodename, device->nodename);
        exit(1);
    }

    addr = device->inet6_addr;
    strncpy(found_device_nodename, device->nodename, MAX_NODENAME_LENGTH);
    found = true;
    return true;
}

static void usage(void) {
    fprintf(stderr, "usage: %s [options] [hostname]\n", appname);
    netboot_usage();
    fprintf(stderr, "    --fuchsia         Use fuchsia link local addresses.\n");
}

static struct option netaddr_opts[] = {
    {"fuchsia", no_argument, NULL, 'f'},
    {NULL,      0,           NULL, 0},
};

static bool netaddr_opt_callback(int ch, int argc, char * const *argv) {
    switch (ch) {
        case 'f':
            fuchsia_address = true;
            break;
        default:
            return false;
    }
    return true;
}

int main(int argc, char** argv) {
    appname = argv[0];
    int index = netboot_handle_custom_getopt(argc, argv, netaddr_opts, 1, netaddr_opt_callback);
    if (index < 0) {
        usage();
        return -1;
    }

    argv += index;
    argc -= index;

    if (argc > 1) {
        usage();
    }

    if (argc == 1) {
        hostname = argv[0];
        if (!*hostname || (*hostname == ':' && hostname[1] == '\0'))
            hostname = NULL;
    }

    if (netboot_discover(NB_SERVER_PORT, NULL, on_device, NULL) || !found) {
        fprintf(stderr, "Failed to discover %s\n", hostname?hostname:"");
        return 1;
    }

    if (fuchsia_address) {
        // Make it a valid link-local address by fiddling some bits.
        addr.sin6_addr.s6_addr[11] = 0xFF;
    }

    // Get the string form of the address.
    char addr_s[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr.sin6_addr, addr_s, sizeof(addr_s));

    // Get the name of the interface.
    char ifname[IF_NAMESIZE];
    if_indextoname(addr.sin6_scope_id, ifname);
    fprintf(stdout, "%s%%%s\n", addr_s, ifname);

    return 0;
}
