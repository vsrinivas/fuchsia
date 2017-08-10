// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <magenta/syscalls.h>
#include <magenta/device/power.h>

typedef struct {
    int type;
    int fd;
    mx_handle_t h;
} pwrdev_t;

static const char* type_to_string[] = { "AC", "battery" };

static int get_battery_info(int fd) {
    battery_info_t binfo;
    ssize_t rc = ioctl_power_get_battery_info(fd, &binfo);
    if (rc != sizeof(binfo)) {
        printf("ioctl() returned %zd\n", rc);
        return -1;
    }
    const char* unit = (binfo.unit == BATTERY_UNIT_MW) ? "mW" : "mA";
    printf("             design capacity: %d %s\n", binfo.design_capacity, unit);
    printf("          last full capacity: %d %s\n", binfo.last_full_capacity, unit);
    printf("              design voltage: %d mV\n", binfo.design_voltage);
    printf("            warning capacity: %d %s\n", binfo.capacity_warning, unit);
    printf("                low capacity: %d %s\n", binfo.capacity_low, unit);
    printf("     low/warning granularity: %d %s\n",
            binfo.capacity_granularity_low_warning, unit);
    printf("    warning/full granularity: %d %s\n",
            binfo.capacity_granularity_warning_full, unit);
    printf("                present rate: %d %s\n", binfo.present_rate, unit);
    printf("          remaining capacity: %d %s\n", binfo.remaining_capacity, unit);
    printf("             present voltage: %d mV\n", binfo.present_voltage);
    printf("==========================================\n");
    printf("remaining battery percentage: %d %%\n",
            binfo.remaining_capacity * 100 / binfo.last_full_capacity);
    if (binfo.present_rate < 0) {
        printf("      remaining battery life: %.2f h\n",
                (float)binfo.remaining_capacity / (float)binfo.present_rate * -1);
    }
    printf("\n");
    return 0;
}

int main(int argc, char** argv) {
    struct dirent* de;
    DIR* dir = opendir("/dev/class/power");
    if (!dir) {
        printf("opendir() returned error\n");
        return -1;
    }
    pwrdev_t dev[2];
    int idx = 0;
    while ((de = readdir(dir)) != NULL) {
        int fd = openat(dirfd(dir), de->d_name, O_RDONLY);
        if (fd < 0) {
            printf("openat() returned %d\n", fd);
            return fd;
        }

        power_info_t info;
        ssize_t rc = ioctl_power_get_info(fd, &info);
        if (rc != sizeof(info)) {
            printf("ioctl() returned %zd\n", rc);
            return -1;
        }

        printf("index: %d type: %s state: 0x%x\n", idx, type_to_string[info.type], info.state);

        if (info.type == POWER_TYPE_BATTERY) {
            if (get_battery_info(fd) < 0) {
                return -1;
            }
        }

        mx_handle_t h = MX_HANDLE_INVALID;
        if (idx < (int)countof(dev)) {
            rc = ioctl_power_get_state_change_event(fd, &h);
            if (rc != sizeof(mx_handle_t)) {
                printf("ioctl() returned %zd\n", rc);
                return -1;
            }
        } else {
            break;
        }

        dev[idx].type = info.type;
        dev[idx].fd = fd;
        dev[idx].h = h;
        idx += 1;
    }

    printf("waiting for events...\n\n");

    for (;;) {
        mx_wait_item_t items[2];
        for (int i = 0; i < idx; i++) {
            items[i].handle = dev[i].h;
            items[i].waitfor = MX_USER_SIGNAL_0;
            items[i].pending = 0;
        }
        mx_status_t status = mx_object_wait_many(items, idx, MX_TIME_INFINITE);
        if (status != MX_OK) {
            printf("mx_object_wait_many() returned %d\n", status);
            return -1;
        }

        for (int i = 0; i < idx; i++) {
            if (items[i].pending & MX_USER_SIGNAL_0) {
                power_info_t info;
                ssize_t rc = ioctl_power_get_info(dev[i].fd, &info);
                if (rc != sizeof(info)) {
                    printf("ioctl() returned %zd\n", rc);
                    return -1;
                }
                printf("got event for %s (%d) new state 0x%x\n",
                       type_to_string[dev[i].type], i, info.state);
                if (dev[i].type == POWER_TYPE_BATTERY) {
                    if (get_battery_info(dev[i].fd) < 0) {
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}
