// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

// Returns a handle to the system loader service, a channel that speaks
// the protocol expected by dl_set_loader_service().  Should only be
// used by mxio: see mxio/loader-service.h.
#define IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DMCTL, 0)

typedef struct {
    mx_handle_t h;
    char name[32];
} dmctl_cmd_t;

// Execute a dmctl command, returning output via provided
// socket handle.
#define IOCTL_DMCTL_COMMAND \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DMCTL, 1)

// ssize_t ioctl_dmctl_get_loader_service_channel(int fd, mx_handle_t* channel_out);
IOCTL_WRAPPER_OUT(ioctl_dmctl_get_loader_service_channel,
                  IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL, mx_handle_t);

// ssize_t ioctl_dmctl_command(int fd, dmctl_cmd_t* cmd);
IOCTL_WRAPPER_IN(ioctl_dmctl_command, IOCTL_DMCTL_COMMAND, dmctl_cmd_t);