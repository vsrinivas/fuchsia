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
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ETH_MAC_SIZE 6
#define BUFSIZE 2048

typedef struct {
    const char* device;
    int pause_secs;
    bool setting_promisc;
    bool promisc_on;
    bool dump_regs;
    char* filter_macs;
    int n_filter_macs;
} ethtool_options_t;

int usage(void) {
    fprintf(stderr, "usage: ethtool <network-device> <time> <actions>\n");
    fprintf(stderr, "  network-device must start with '/dev/')\n");
    fprintf(stderr, "  time = how many seconds to hold the fd (before exiting)\n");
    fprintf(stderr, "Actions: one of\n");
    fprintf(stderr, "  promisc on     : Promiscuous mode on\n");
    fprintf(stderr, "  promisc off    : Promiscuous mode off\n");
    fprintf(stderr, "  filter n.n.n.n.n.n n.n.n.n.n.n ...    : multicast filter these addresses\n");
    fprintf(stderr, "  dump           : Dump regs of chip\n");
    fprintf(stderr, "    (empty list is valid)\n");
    fprintf(stderr, "  --help  : Show this help message\n");
    return -1;
}

// Str should be nn.nn.nn.nn.nn.nn where nn is decimal 0..255 and there are ETH_MAC_SIZE (6) nn's
// Function returns non-zero if this isn't the case. (It's not fully paranoid, but shouldn't crash.)
// If input is good, first nn goes into mac[0], etc.
int parse_address(char* mac, const char* str) {
    char* next_string;
    for (int i = 0; i < 5; i++) {
        mac[i] = (char)strtol(str, &next_string, 10);
        if (next_string[0] != '.') {
            return -1;
        }
        next_string++;
        str = next_string;
    }
    mac[5] = (char)strtol(str, &next_string, 10);
    if (next_string[0] != 0) {
        return -1;
    }
    return 0;
}

int parse_args(int argc, const char** argv, ethtool_options_t* options) {
    bool promisc_on = false;
    bool promisc_off = false;

    if (argc < 3) {
        return usage();
    }
    if (strncmp(argv[0], "/dev/", strlen("/dev/"))) {
        return usage();
    }
    options->device = argv[0];
    argc--;
    argv++;

    char* remainder;
    options->pause_secs = strtol(argv[0], &remainder, 10);
    if (options->pause_secs < 0 || remainder[0] != 0) {
        return usage();
    }
    argc--;
    argv++;

    if (!strcmp(argv[0], "promisc")) {
        argc--;
        argv++;
        if (argc != 1) {
            return usage();
        }
        if (!strcmp(argv[0], "on")) {
            promisc_on = true;
        } else if (!strcmp(argv[0], "off")) {
            promisc_off = true;
        } else {
            return usage();
        }
    } else if (!strcmp(argv[0], "dump")) {
        argc--;
        argv++;
        if (argc != 0) {
            return usage();
        }
        options->dump_regs = true;
    } else if (!strcmp(argv[0], "filter")) {
        argc--;
        argv++;
        options->n_filter_macs = argc;
        options->filter_macs = calloc(1, ETH_MAC_SIZE * argc);
        char* addr_ptr = options->filter_macs;
        while (argc--) {
            if (parse_address(addr_ptr, *(argv++))) {
                return usage();
            }
            addr_ptr += ETH_MAC_SIZE;
        }
    } else { // Includes --help, -h, --HELF, --42, etc.
        return usage();
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

int initialize_ethernet(ethtool_options_t* options) {
    int fd;
    if ((fd = open(options->device, O_RDWR)) < 0) {
        fprintf(stderr, "ethtool: cannot open '%s': %d\n", options->device, fd);
        return -1;
    }

    eth_fifos_t fifos;
    zx_status_t status;

    ssize_t r;
    if ((r = ioctl_ethernet_get_fifos(fd, &fifos)) < 0) {
        fprintf(stderr, "ethtool: failed to get fifos: %zd\n", r);
        return r;
    }

    unsigned count = fifos.rx_depth / 2;
    zx_handle_t iovmo;
    // allocate shareable ethernet buffer data heap
    if ((status = zx_vmo_create(count * BUFSIZE, 0, &iovmo)) < 0) {
        return -1;
    }

    if ((r = ioctl_ethernet_set_iobuf(fd, &iovmo)) < 0) {
        fprintf(stderr, "ethtool: failed to set iobuf: %zd\n", r);
        return -1;
    }

    if ((r = ioctl_ethernet_set_client_name(fd, "ethtool", 7)) < 0) {
        fprintf(stderr, "ethtool: failed to set client name %zd\n", r);
    }

    if (ioctl_ethernet_start(fd) < 0) {
        fprintf(stderr, "ethtool: failed to start network interface\n");
        return -1;
    }

    return fd;
}

int main(int argc, const char** argv) {
    ethtool_options_t options;
    memset(&options, 0, sizeof(options));
    if (parse_args(argc - 1, argv + 1, &options)) {
        return -1;
    }

    int fd = initialize_ethernet(&options);
    ssize_t r;
    if (options.setting_promisc) {
        if ((r = ioctl_ethernet_set_promisc(fd, &options.promisc_on)) < 0) {
            fprintf(stderr, "ethtool: failed to set promiscuous mode to %s: %zd\n",
                    options.promisc_on ? "on" : "off", r);
            return -1;
        } else {
            fprintf(stderr, "ethtool: set %s promiscuous mode to %s\n",
                    options.device, options.promisc_on ? "on" : "off");
        }
    }
    if (options.filter_macs) {
        eth_multicast_config_t config;
        memset(&config, 0, sizeof(config));
        config.op = ETH_MULTICAST_TEST_FILTER;
        if ((r = ioctl_ethernet_config_multicast(fd, &config)) < 0) {
            fprintf(stderr, "ethtool: failed to config multicast test\n");
            return -1;
        }
        config.op = ETH_MULTICAST_ADD_MAC;
        for (int i = 0; i < options.n_filter_macs; i++) {
            memcpy(config.mac, options.filter_macs + i * ETH_MAC_SIZE, ETH_MAC_SIZE);
            printf("Sending addr %d %d %d %d %d %d\n", config.mac[0], config.mac[1],
                   config.mac[2], config.mac[3], config.mac[4], config.mac[5]);
            if ((r = ioctl_ethernet_config_multicast(fd, &config)) < 0) {
                fprintf(stderr, "ethtool: failed to add multicast addr\n");
                return -1;
            }
        }
    }
    if (options.dump_regs) {
        eth_multicast_config_t config;
        memset(&config, 0, sizeof(config));
        config.op = ETH_MULTICAST_DUMP_REGS;
        if ((r = ioctl_ethernet_config_multicast(fd, &config)) < 0) {
            fprintf(stderr, "ethtool: failed to request reg dump\n");
            return -1;
        }
    }
    zx_nanosleep(zx_deadline_after(ZX_SEC(options.pause_secs)));
    return 0;
}
