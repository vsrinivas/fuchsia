// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/sysinfo.h>
#include <zircon/device/thermal.h>
#include <zircon/syscalls/system.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <fdio/watcher.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// TODO(braval): Combine thermd & thermd_arm and have a unified
// code for the thermal deamon
static zx_status_t thermal_device_added(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    if (!strcmp("000", name)) {
        // Device found, terminate watcher
        return ZX_ERR_STOP;
    } else {
        return ZX_OK;
    }
}

int main(int argc, char** argv) {
    printf("thermd: started\n");

    // TODO(braval): This sleep is not needed here but leaving it here
    // since the Interl thermd has it. Clean up when both deamons are
    // unified
    zx_nanosleep(zx_deadline_after(ZX_SEC(3)));

    int dirfd = open("/dev/class/thermal", O_DIRECTORY | O_RDONLY);
    if (dirfd < 0) {
        fprintf(stderr, "ERROR: Failed to open /dev/class/thermal: %d (errno %d)\n",
                dirfd, errno);
        return -1;
    }

    zx_status_t st = fdio_watch_directory(dirfd, thermal_device_added, ZX_TIME_INFINITE, NULL);
    if (st != ZX_ERR_STOP) {
        fprintf(stderr, "ERROR: watcher terminating without finding sensors, "
                        "terminating thermd...\n");
        return -1;
    }

    // first device is the one we are interested
    int fd = open("/dev/class/thermal/000", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to open sensor: %d\n", fd);
        return -1;
    }

    thermal_device_info_t info;
    ssize_t rc = ioctl_thermal_get_device_info(fd, &info);
    if (rc != sizeof(info)) {
        fprintf(stderr, "ERROR: Failed to get thermal info: %zd\n", rc);
        return rc;
    }

    if (info.num_trip_points == 0) {
        fprintf(stderr, "Trip points not supported, exiting\n");
        return 0;
    }

    zx_handle_t port = ZX_HANDLE_INVALID;
    rc = ioctl_thermal_get_state_change_port(fd, &port);
    if (rc != sizeof(port)) {
        fprintf(stderr, "ERROR: Failed to get event: %zd\n", rc);
        return rc;
    }

    for (;;) {
        zx_port_packet_t packet;
        st = zx_port_wait(port, ZX_TIME_INFINITE,
                          &packet, 1);
        if (st != ZX_OK) {
            fprintf(stderr, "ERROR: Failed to wait on port: %d\n", st);
            return st;
        }

        // For now we only use the ports to send messages about
        // the trip point. In future we could posssibly send more
        // iformation and incorporate DVFS also here. When we do
        // that this would be more of a power/thermal management
        // deamon rather than thermal deamon
        if (info.active_cooling) {
            uint32_t fan_level = (uint32_t)packet.key;
            if (fan_level >= 0 && fan_level < info.num_fan_level) {
                rc = ioctl_thermal_set_fan_level(fd, &fan_level);
                if (rc) {
                    fprintf(stderr, "ERROR: Failed to set fan level: %zd\n", rc);
                }
            }
        }

    }

    close(fd);

    printf("thermd terminating: %d\n", st);

    return 0;
}
