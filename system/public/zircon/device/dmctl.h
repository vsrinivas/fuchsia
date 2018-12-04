// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

typedef struct {
    zx_handle_t h;
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
    zx_handle_t kernel;
    zx_handle_t bootdata;
} dmctl_mexec_args_t;

// Soft reboot the system with a new kernel and bootdata.
// Passes a handle to the kernel vmo and a handle to the bootdata vmo.
// The bootdata vmo should contain the cmdline.
// If successful, this ioctl does not return.
#define IOCTL_DMCTL_MEXEC \
    IOCTL(IOCTL_KIND_SET_TWO_HANDLES, IOCTL_FAMILY_DMCTL, 4)

// ssize_t ioctl_dmctl_command(int fd, dmctl_cmd_t* cmd);
IOCTL_WRAPPER_IN(ioctl_dmctl_command, IOCTL_DMCTL_COMMAND, dmctl_cmd_t);

// ssize_t ioctl_dmctl_open_virtcon(int fd, zx_handle_t* h);
IOCTL_WRAPPER_IN(ioctl_dmctl_open_virtcon, IOCTL_DMCTL_OPEN_VIRTCON, zx_handle_t);

// ssize_t ioctl_dmctl_mexec(int fd, dmctl_mexec_args_t* args);
IOCTL_WRAPPER_IN(ioctl_dmctl_mexec, IOCTL_DMCTL_MEXEC, dmctl_mexec_args_t);
