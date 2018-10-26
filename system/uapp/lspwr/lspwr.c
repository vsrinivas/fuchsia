// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/power/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <lib/fdio/util.h>

typedef struct {
    int type;
    zx_handle_t fidl_channel;
    zx_handle_t events_handle;
} pwrdev_t;

static const char* type_to_string[] = {"AC", "battery"};

static zx_status_t get_battery_info(zx_handle_t ch) {
    struct fuchsia_power_BatteryInfo binfo = {};
    zx_status_t status = fuchsia_power_SourceGetBatteryInfo(ch, &binfo);
    if (status != ZX_OK) {
        printf("GetBatteryInfo returned %d\n", status);
        return status;
    }

    const char* unit = (binfo.unit == fuchsia_power_BatteryUnit_MW) ? "mW" : "mA";
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
    return ZX_OK;
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

        struct fuchsia_power_SourceInfo pinfo;
        zx_handle_t ch;
        zx_handle_t status = fdio_get_service_handle(fd, &ch);
        if (status != ZX_OK) {
            printf("Failed to get service handle: %d!\n", status);
            return status;
        }

        status = fuchsia_power_SourceGetPowerInfo(ch, &pinfo);
        if (status != ZX_OK) {
            printf("GetPowerInfo retruned %d\n", status);
            return status;
        }

        printf("index: %d type: %s state: 0x%x\n", idx, type_to_string[pinfo.type], pinfo.state);

        if (pinfo.type == fuchsia_power_PowerType_BATTERY) {
            if (get_battery_info(ch) != ZX_OK) {
                return -1;
            }
        }

        zx_handle_t h = ZX_HANDLE_INVALID;
        if (idx < (int)countof(dev)) {
            zx_status_t c_status;
            status = fuchsia_power_SourceGetStateChangeEvent(ch, &c_status, &h);
            if (status != ZX_OK) {
                return status;
            }
        } else {
            break;
        }

        dev[idx].type = pinfo.type;
        dev[idx].fidl_channel = ch;
        dev[idx].events_handle = h;
        idx += 1;
    }

    printf("waiting for events...\n\n");

    for (;;) {
        zx_wait_item_t items[2];
        for (int i = 0; i < idx; i++) {
            items[i].handle = dev[i].events_handle;
            items[i].waitfor = ZX_USER_SIGNAL_0;
            items[i].pending = 0;
        }
        zx_status_t status = zx_object_wait_many(items, idx, ZX_TIME_INFINITE);
        if (status != ZX_OK) {
            printf("zx_object_wait_many() returned %d\n", status);
            return -1;
        }

        for (int i = 0; i < idx; i++) {
            if (items[i].pending & ZX_USER_SIGNAL_0) {
                struct fuchsia_power_SourceInfo info;
                ssize_t rc = fuchsia_power_SourceGetPowerInfo(dev[i].fidl_channel, &info);
                if (rc != sizeof(info)) {
                    printf("ioctl() returned %zd\n", rc);
                    return -1;
                }
                printf("got event for %s (%d) new state 0x%x\n",
                       type_to_string[dev[i].type], i, info.state);
                if (dev[i].type == fuchsia_power_PowerType_BATTERY) {
                    if (get_battery_info(dev[i].fidl_channel) != ZX_OK) {
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}
