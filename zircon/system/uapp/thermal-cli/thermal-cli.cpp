// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

constexpr char kUsageMessage[] = R"""(
Usage: thermal-cli <device> <command>

    temp - Read the device's thermal sensor in degrees C
    fan [value] - Get or set the fan speed
    freq <big/little> [value] - Get or set the cluster frequency in Hz
)""";

int TempCommand(const zx::channel& handle) {
    zx_status_t status, status2;
    uint32_t temp;
    status = fuchsia_hardware_thermal_DeviceGetTemperature(handle.get(), &status2, &temp);
    if (status != ZX_OK || status2 != ZX_OK) {
        printf("DeviceGetTemperature failed: %d %d\n", status, status2);
        return 1;
    }

    printf("Temperature: %u\n", temp);
    return 0;
}

int FanCommand(const zx::channel& handle, const char* value) {
    zx_status_t status, status2;

    if (value == nullptr) {
        uint32_t fan_level;
        status = fuchsia_hardware_thermal_DeviceGetFanLevel(handle.get(), &status2, &fan_level);
        if (status != ZX_OK || status2 != ZX_OK) {
            printf("DeviceSetFanLevel failed: %d %d\n", status, status2);
            return 1;
        }

        printf("Fan level: %u\n", fan_level);
    } else {
        int fan_level = atoi(value);
        status = fuchsia_hardware_thermal_DeviceSetFanLevel(handle.get(), fan_level, &status2);
        if (status != ZX_OK || status2 != ZX_OK) {
            printf("DeviceSetFanLevel failed: %d %d\n", status, status2);
            return 1;
        }
    }

    return 0;
}

int FreqCommand(const zx::channel& handle, uint32_t cluster, const char* value) {
    zx_status_t status, status2;
    fuchsia_hardware_thermal_OperatingPoint op_info;
    status = fuchsia_hardware_thermal_DeviceGetDvfsInfo(handle.get(), cluster, &status2, &op_info);
    if (status != ZX_OK || status2 != ZX_OK) {
        printf("DeviceGetDvfsInfo failed: %d %d\n", status, status2);
        return 1;
    } else if (op_info.count > fuchsia_hardware_thermal_MAX_DVFS_OPPS) {
        printf("DeviceGetDvfsInfo reported to many operating points\n");
        return 1;
    }

    if (value == nullptr) {
        uint16_t op_idx;
        status = fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(handle.get(), cluster,
                                                                      &status2, &op_idx);
        if (status != ZX_OK || status2 != ZX_OK) {
            printf("DeviceGetDvfsOperatingPoint failed: %d %d\n", status, status2);
            return 1;
        } else if (op_idx > op_info.count) {
            printf("DeviceGetDvfsOperatingPoint reported an invalid operating point\n");
        }

        printf("Current frequency: %u Hz\n", op_info.opp[op_idx].freq_hz);

        printf("Operating points:\n");
        for (uint32_t i = 0; i < op_info.count; i++) {
            printf("%u Hz\n", op_info.opp[i].freq_hz);
        }
    } else {
        long freq = atol(value);
        uint16_t op_idx;
        for (op_idx = 0; op_idx < op_info.count; op_idx++) {
            if (op_info.opp[op_idx].freq_hz == freq) {
                break;
            }
        }

        if (op_idx >= op_info.count) {
            printf("No operating point found for %ld Hz\n", freq);

            printf("Operating points:\n");
            for (uint32_t i = 0; i < op_info.count; i++) {
                printf("%u Hz\n", op_info.opp[i].freq_hz);
            }

            return 1;
        }

        status = fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(handle.get(), op_idx, cluster,
                                                                      &status2);
        if (status != ZX_OK || status2 != ZX_OK) {
            printf("DeviceSetDvfsOperatingPoint failed: %d %d\n", status, status2);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("%s\n", kUsageMessage);
        return 0;
    }

    fbl::unique_fd fd(open(argv[1], O_RDWR));
    if (fd.get() < -1) {
        printf("Failed to open thermal device: %d\n", fd.get());
        return 1;
    }

    zx::channel handle;
    zx_status_t status = fdio_get_service_handle(fd.release(), handle.reset_and_get_address());
    if (status != ZX_OK) {
        printf("Failed to get FDIO handle for thermal device: %d\n", status);
        return 1;
    }

    if (strcmp(argv[2], "temp") == 0) {
        return TempCommand(handle);
    } else if (strcmp(argv[2], "fan") == 0) {
        return FanCommand(handle, argc >= 4 ? argv[3] : nullptr);
    } else if (strcmp(argv[2], "freq") == 0 && argc >= 4) {
        uint32_t cluster = fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN;
        if (strcmp(argv[3], "little") == 0) {
            cluster = fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN;
        }

        return FreqCommand(handle, cluster, argc >= 5 ? argv[4] : nullptr);
    }

    printf("%s\n", kUsageMessage);
    return 1;
}
