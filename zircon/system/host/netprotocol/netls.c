// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "netprotocol.h"

#define MAX_DEVICES 255

static device_info_t devices[MAX_DEVICES];
static uint32_t devices_count = 0;

static const char* appname;

static bool has_device(const char* nodename) {
    for (uint32_t i = 0; i < devices_count; ++i) {
        if (!strncmp(devices[i].nodename, nodename, sizeof(devices[i].nodename))) {
            return true;
        }
    }
    return false;
}

static device_info_t* get_device(const char* nodename) {
    for (uint32_t i = 0; i < devices_count; ++i) {
        if (!strncmp(devices[i].nodename, nodename, sizeof(devices[i].nodename))) {
            return &devices[i];
        }
    }
    return NULL;
}

static device_info_t* add_device(device_info_t* device) {
    device_info_t* known_device = get_device(device->nodename);
    if (!known_device) {
        if (devices_count > MAX_DEVICES) {
            return NULL;
        }
        known_device = &devices[devices_count];
        devices_count++;
        strncpy(known_device->nodename, device->nodename, sizeof(known_device->nodename));
    }
    strncpy(known_device->inet6_addr_s, device->inet6_addr_s, INET6_ADDRSTRLEN);
    memcpy(&known_device->inet6_addr, &device->inet6_addr, sizeof(known_device->inet6_addr));
    known_device->state = device->state;
    known_device->bootloader_port = device->bootloader_port;
    known_device->bootloader_version = device->bootloader_version;
    return known_device;
}

static bool on_device(device_info_t* device, void* cookie) {
    if (!has_device(device->nodename)) {
        if (device->state == UNKNOWN) {
            device->state = OFFLINE;
        }
        const char* state = "unknown";
        switch (device->state) {
        case UNKNOWN:
            state = "unknown";
            break;
        case OFFLINE:
            state = "offline";
            break;
        case DEVICE:
            state = "device";
            break;
        case BOOTLOADER:
            state = "bootloader";
            break;
        }

        // TODO(jimbe): Print the type of the device based on the vendor id of the mac address.
        fprintf(stdout, "%10s %s", state, device->nodename);
        if (device->inet6_addr.sin6_scope_id != 0) {
            fprintf(stdout, " (%s/%d)", device->inet6_addr_s, device->inet6_addr.sin6_scope_id);
        }
        if (device->state == BOOTLOADER) {
            fprintf(stdout, " [Bootloader version 0x%08X listening on %d]",
                    device->bootloader_version, device->bootloader_port);
        }
        fprintf(stdout, "\n");
        if (add_device(device) == NULL) {
            return false;
        }
    }
    return true;
}

static void usage(void) {
    fprintf(stderr, "usage: %s [options]\n", appname);
    netboot_usage(false);
}

int main(int argc, char** argv) {
    appname = argv[0];
    int index = netboot_handle_getopt(argc, argv);
    if (index < 0) {
        usage();
        return -1;
    }

    if (netboot_discover(NB_SERVER_PORT, NULL, on_device, NULL)) {
        fprintf(stderr, "Failed to discover\n");
        return 1;
    }
    return 0;
}
