// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

#define IOCTL_RAMDISK_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 1)
#define IOCTL_RAMDISK_CONFIG_VMO \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_RAMDISK, 4)
#define IOCTL_RAMDISK_UNLINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 2)
#define IOCTL_RAMDISK_SET_FLAGS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 3)
#define IOCTL_RAMDISK_WAKE_UP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 4)
#define IOCTL_RAMDISK_SLEEP_AFTER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 5)
#define IOCTL_RAMDISK_GET_TXN_COUNT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 6)

typedef struct ramdisk_ioctl_config {
    uint64_t blk_size;
    uint64_t blk_count;
} ramdisk_ioctl_config_t;

typedef struct ramdisk_ioctl_config_response {
    char name[NAME_MAX + 1];
} ramdisk_ioctl_config_response_t;

// ssize_t ioctl_ramdisk_config(int fd, const ramdisk_ioctl_config_t* in,
//                              ramdisk_ioctl_config_response_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ramdisk_config, IOCTL_RAMDISK_CONFIG, ramdisk_ioctl_config_t,
                    ramdisk_ioctl_config_response_t);

// ssize_t ioctl_ramdisk_config_vmo(int fd, const zx_handle_t* in,
//                                  ramdisk_ioctl_config_response_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ramdisk_config_vmo, IOCTL_RAMDISK_CONFIG_VMO,
                    zx_handle_t, ramdisk_ioctl_config_response_t);

// ssize_t ioctl_ramdisk_unlink(int fd);
IOCTL_WRAPPER(ioctl_ramdisk_unlink, IOCTL_RAMDISK_UNLINK);

// ssize_t ioctl_ramdisk_set_flags(int fd, uint32_t* in);
// The flags to set match block_info_t.flags. This is intended to simulate the behavior
// of other block devices, so it should be used only for tests.
IOCTL_WRAPPER_IN(ioctl_ramdisk_set_flags, IOCTL_RAMDISK_SET_FLAGS, uint32_t);

// ssize_t ioctl_ramdisk_wake_up(int fd);
// "Wakes" the ramdisk, if it was sleeping.
// Transactions are no longer expected to fail after this point, and the ramdisk will not sleep
// again until the next call to SLEEP_AFTER.
// This will reset the current transaction count.
IOCTL_WRAPPER(ioctl_ramdisk_wake_up, IOCTL_RAMDISK_WAKE_UP);

// ssize_t ioctl_ramdisk_sleep_after(int fd, uint64_t* in);
// Tell the ramdisk to "sleep" after |in| transactions.
// After this point, all incoming transactions will fail.
// This will reset the current transaction count.
IOCTL_WRAPPER_IN(ioctl_ramdisk_sleep_after, IOCTL_RAMDISK_SLEEP_AFTER, uint64_t);

// ssize_t ioctl_ramdisk_get_txn_count(int fd, uint64_t* out);
// Retrieve the number of successful transactions since the last call to sleep/wake.
IOCTL_WRAPPER_OUT(ioctl_ramdisk_get_txn_count, IOCTL_RAMDISK_GET_TXN_COUNT, uint64_t);