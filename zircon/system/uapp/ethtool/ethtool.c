// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <inet6/inet6.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <zircon/assert.h>
#include <zircon/boot/netboot.h>
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

zx_handle_t initialize_ethernet(ethtool_options_t* options) {
    int fd;
    if ((fd = open(options->device, O_RDWR)) < 0) {
        fprintf(stderr, "ethtool: cannot open '%s': %d\n", options->device, fd);
        return ZX_HANDLE_INVALID;
    }

    zx_handle_t svc;
    zx_status_t status = fdio_get_service_handle(fd, &svc);
    if (status != ZX_OK) {
        fprintf(stderr, "ethtool: failed to get service handle\n");
        return ZX_HANDLE_INVALID;
    }

    fuchsia_hardware_ethernet_Fifos fifos;

    zx_status_t call_status = ZX_OK;
    status = fuchsia_hardware_ethernet_DeviceGetFifos(svc, &call_status, &fifos);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "ethtool: failed to get fifos: %d, %d\n", status, call_status);
        return ZX_HANDLE_INVALID;
    }

    unsigned count = fifos.rx_depth / 2;
    zx_handle_t iovmo;
    // allocate shareable ethernet buffer data heap
    if ((status = zx_vmo_create(count * BUFSIZE, 0, &iovmo)) < 0) {
        return ZX_HANDLE_INVALID;
    }

    status = fuchsia_hardware_ethernet_DeviceSetIOBuffer(svc, iovmo, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "ethtool: failed to set iobuf: %d, %d\n", status, call_status);
        return ZX_HANDLE_INVALID;
    }

    status = fuchsia_hardware_ethernet_DeviceSetClientName(svc, "ethtool", 7, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "ethtool: failed to set client name %d, %d\n", status, call_status);
    }

    status = fuchsia_hardware_ethernet_DeviceStart(svc, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "ethtool: failed to start network interface\n");
        return ZX_HANDLE_INVALID;
    }

    return svc;
}

int main(int argc, const char** argv) {
    ethtool_options_t options;
    memset(&options, 0, sizeof(options));
    if (parse_args(argc - 1, argv + 1, &options)) {
        return -1;
    }

    zx_handle_t svc = initialize_ethernet(&options);
    if (svc == ZX_HANDLE_INVALID) {
        return -1;
    }
    zx_status_t status, call_status;
    if (options.setting_promisc) {
        status = fuchsia_hardware_ethernet_DeviceSetPromiscuousMode(svc, options.promisc_on,
                                                                    &call_status);
        if (status != ZX_OK || call_status != ZX_OK) {
            fprintf(stderr, "ethtool: failed to set promiscuous mode to %s: %d, %d\n",
                    options.promisc_on ? "on" : "off", status, call_status);
            return -1;
        } else {
            fprintf(stderr, "ethtool: set %s promiscuous mode to %s\n",
                    options.device, options.promisc_on ? "on" : "off");
        }
    }
    if (options.filter_macs) {
        status = fuchsia_hardware_ethernet_DeviceConfigMulticastTestFilter(svc, &call_status);
        if (status != ZX_OK || call_status != ZX_OK) {
            fprintf(stderr, "ethtool: failed to config multicast test\n");
            return -1;
        }
        for (int i = 0; i < options.n_filter_macs; i++) {
            fuchsia_hardware_ethernet_MacAddress addr;
            memcpy(&addr.octets, options.filter_macs + i * ETH_MAC_SIZE, ETH_MAC_SIZE);
            printf("Sending addr %d %d %d %d %d %d\n", addr.octets[0], addr.octets[1],
                   addr.octets[2], addr.octets[3], addr.octets[4], addr.octets[5]);
            status =
                fuchsia_hardware_ethernet_DeviceConfigMulticastAddMac(svc, &addr, &call_status);
            if (status != ZX_OK || call_status != ZX_OK) {
                fprintf(stderr, "ethtool: failed to add multicast addr\n");
                return -1;
            }
        }
    }
    if (options.dump_regs) {
        status = fuchsia_hardware_ethernet_DeviceDumpRegisters(svc, &call_status);
        if (status != ZX_OK || call_status != ZX_OK) {
            fprintf(stderr, "ethtool: failed to request reg dump\n");
            return -1;
        }
    }
    zx_nanosleep(zx_deadline_after(ZX_SEC(options.pause_secs)));
    return 0;
}
