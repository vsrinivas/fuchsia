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
    char name[64];
} dmctl_cmd_t;

// Execute a dmctl command, returning output via provided
// socket handle.
#define IOCTL_DMCTL_COMMAND \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DMCTL, 1)

// Open a new virtual console
// Pass a channel handle.
// On success one or two handles will be written back (a remoteio device)
// On failure the channel will be closed.
#define IOCTL_DMCTL_OPEN_VIRTCON \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DMCTL, 2)

// Install a channel to receive updates on devices and drivers in the system
// This is an experimental, non-stable interface.  Only one client is supported.
// Any later calls will disconnect earlier clients.
// One message will be sent on the provided channel per devmgr_event_t
#define IOCTL_DMCTL_WATCH_DEVMGR \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DMCTL, 3)

typedef struct {
    uint32_t opcode;
    uint32_t flags;
    uint64_t id;
    union {
        struct {
            uint32_t protocol_id;
            // header is followed by props_len mx_device_prop_t
            // and path_len of topological path (no \0 terminator)
            uint32_t props_len;
            uint32_t path_len;
            uint32_t reserved;
        } add;
    } u;
} devmgr_event_t;

// Device id has been added
#define DEVMGR_OP_DEVICE_ADDED 1

// Device id has been removed
#define DEVMGR_OP_DEVICE_REMOVED 2

// Device id has changed state (check flags)
#define DEVMGR_OP_DEVICE_CHANGED 3

// A driver is bound to this device
#define DEVMGR_FLAGS_BOUND 1


// ssize_t ioctl_dmctl_get_loader_service_channel(int fd, mx_handle_t* channel_out);
IOCTL_WRAPPER_OUT(ioctl_dmctl_get_loader_service_channel,
                  IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL, mx_handle_t);

// ssize_t ioctl_dmctl_command(int fd, dmctl_cmd_t* cmd);
IOCTL_WRAPPER_IN(ioctl_dmctl_command, IOCTL_DMCTL_COMMAND, dmctl_cmd_t);

// ssize_t ioctl_dmctl_open_virtcon(int fd, dmctl_cmd_t* cmd);
IOCTL_WRAPPER_IN(ioctl_dmctl_open_virtcon, IOCTL_DMCTL_OPEN_VIRTCON, mx_handle_t);

// ssize_t ioctl_dmctl_watch_devmgr(int fd, dmctl_cmd_t* cmd);
IOCTL_WRAPPER_IN(ioctl_dmctl_watch_devmgr, IOCTL_DMCTL_WATCH_DEVMGR, mx_handle_t);