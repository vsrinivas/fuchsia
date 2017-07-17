// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

// return a handle usable with mx_ktrace_write()
#define IOCTL_KTRACE_GET_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_KTRACE, 1)

// define a new ktrace probe name
// input: ascii probe name, < MX_MAX_NAME_LEN
// reply: uint32_t probe id usable with mx_ktrace_write()
#define IOCTL_KTRACE_ADD_PROBE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_KTRACE, 2)

IOCTL_WRAPPER_OUT(ioctl_ktrace_get_handle, IOCTL_KTRACE_GET_HANDLE, mx_handle_t);
// Start tracing.
// input: The group_mask
#define IOCTL_KTRACE_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_KTRACE, 3)

// Stop
#define IOCTL_KTRACE_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_KTRACE, 4)

static inline mx_status_t ioctl_ktrace_add_probe(int fd, const char* name, uint32_t* probe_id) {
    return mxio_ioctl(fd, IOCTL_KTRACE_ADD_PROBE,
                      name, strlen(name), probe_id, sizeof(uint32_t));
}

IOCTL_WRAPPER_IN(ioctl_ktrace_start, IOCTL_KTRACE_START, uint32_t);
IOCTL_WRAPPER(ioctl_ktrace_stop, IOCTL_KTRACE_STOP);
