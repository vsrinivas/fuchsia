// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inet6/inet6.h>
#include <zircon/assert.h>
#include <zircon/boot/netboot.h>
#include <zircon/device/ethernet.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char* device;
    bool setting_promisc;
    bool promisc_on;
} ethtool_options_t;

int usage(void) {
    fprintf(stderr, "usage: ethtool <network-device> <actions>\n");
    fprintf(stderr, "  network-device must start with '/dev/')\n");
    fprintf(stderr, "Actions:\n");
    fprintf(stderr, "  promisc on     : Promiscuous mode on\n");
    fprintf(stderr, "  promisc off    : Promiscuous mode off\n");
    fprintf(stderr, "  --help  : Show this help message\n");
    return -1;
}

int parse_args(int argc, const char** argv, ethtool_options_t* options) {
    bool promisc_on = false;
    bool promisc_off = false;

    if (argc < 1) {
        return usage();
    }
    if (strncmp(argv[0], "/dev/", strlen("/dev/"))) {
        return usage();
    }
    options->device = argv[0];
    argc--;
    argv++;

    while (argc > 0) {
        if (!strcmp(argv[0], "promisc")) {
            argc--;
            argv++;
            if (argc < 1) {
                return usage();
            }
            if (!strcmp(argv[0], "on")) {
                promisc_on = true;
            } else if (!strcmp(argv[0], "off")) {
                promisc_off = true;
            } else {
                return usage();
            }
        } else { // Includes --help, -h, --HELF, --42, etc.
            return usage();
        }
        argc--;
        argv++;
    }

    if (promisc_on && promisc_off) {
        return usage();
    }
    if (promisc_on || promisc_off) {
        options->setting_promisc = true;
        options->promisc_on = promisc_on;
    }

    return 0;
}

int main(int argc, const char** argv) {
    ethtool_options_t options;
    memset(&options, 0, sizeof(options));
    if (parse_args(argc - 1, argv + 1, &options)) {
        return -1;
    }

    int fd;
    if ((fd = open(options.device, O_RDWR)) < 0) {
        fprintf(stderr, "ethtool: cannot open '%s': %d\n", options.device, fd);
        return -1;
    }

    ssize_t r;
    if ((r = ioctl_ethernet_set_client_name(fd, "ethtool", 7)) < 0) {
        fprintf(stderr, "ethtool: failed to set client name %zd\n", r);
    }

    if (options.setting_promisc) {
        if ((r = ioctl_ethernet_set_promisc(fd, &options.promisc_on)) < 0) {
            fprintf(stderr, "ethtool: failed to set promiscuous mode to %s: %zd\n",
                    options.promisc_on ? "on" : "off", r);
        } else {
            fprintf(stderr, "ethtool: set %s promiscuous mode to %s\n",
                    options.device, options.promisc_on ? "on" : "off");
        }
    }

    return 0;
}
