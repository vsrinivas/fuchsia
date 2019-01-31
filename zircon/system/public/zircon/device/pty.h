// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/device.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

// A PTY (pseudoterminal) emulates terminal devices, with a
// "server" side (which represents the keyboard+monitor side
// of the terminal and is obtained by opening /dev/misc/ptmx)
// and a number of "client" sides which are obtained by doing
// an open_at(server_pty_fd, "0", O_RDWR) or
// open_at(client_0_fd, "#", O_RDWR).
//
// Client PTYs are identified by the unsigned number used in
// the open_at().  The first Client PTY *must* be 0, and it is
// the only Client PTY that is allowed to create additional
// Client PTYs, receive Events, etc.  It is the Controlling PTY.

// IOCTLs allowed on Client PTYs
// -----------------------------

// Clear and/or Set PTY Features
//  in: pty_clr_set_t change
// out: uint32_t features
#define IOCTL_PTY_CLR_SET_FEATURE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PTY, 0x00)

typedef struct {
    uint32_t clr;
    uint32_t set;
} pty_clr_set_t;

// When Feature Raw is enabled, OOB Events like ^c, ^z, etc
// are not generated.  Instead the character is read from the
// read() input path.
#define PTY_FEATURE_RAW 1

// Obtain the window size (in character cells)
//  in: none
// out: pty_window_size_t
#define IOCTL_PTY_GET_WINDOW_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PTY, 0x01)

typedef struct {
    uint32_t width;
    uint32_t height;
} pty_window_size_t;

// IOCTLs allowed on the Controlling PTY
// -------------------------------------

// Select which Client PTY receives input.
// Reads will simply block on non-active PTYs.
//
//  in: uint32_t client_pty_id
// out: none
#define IOCTL_PTY_MAKE_ACTIVE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PTY, 0x10)

// Returns pending OOB events, simultaneously clearing them
//
//  in: none
// out: uint32_t events
#define IOCTL_PTY_READ_EVENTS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PTY, 0x13)

#define PTY_EVENT_HANGUP    (1u) // no active client
#define PTY_EVENT_INTERRUPT (2u) // ^c
#define PTY_EVENT_SUSPEND   (4u) // ^z
#define PTY_EVENT_MASK      (7u) // all events

// When an event is pending, this signal is asserted
// On the Controlling Client PTY
#define PTY_SIGNAL_EVENT DEVICE_SIGNAL_OOB

// IOCTLs allowed on the Server PTY
// --------------------------------

// Sets the window size
//
//  in: pty_window_size_t
// out: none
#define IOCTL_PTY_SET_WINDOW_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PTY, 0x20)

IOCTL_WRAPPER_IN(ioctl_pty_clr_set_feature, IOCTL_PTY_CLR_SET_FEATURE, pty_clr_set_t);
IOCTL_WRAPPER_OUT(ioctl_pty_get_window_size, IOCTL_PTY_GET_WINDOW_SIZE, pty_window_size_t);

IOCTL_WRAPPER_IN(ioctl_pty_make_active, IOCTL_PTY_MAKE_ACTIVE, uint32_t);
IOCTL_WRAPPER_OUT(ioctl_pty_read_events, IOCTL_PTY_READ_EVENTS, uint32_t);

IOCTL_WRAPPER_IN(ioctl_pty_set_window_size, IOCTL_PTY_SET_WINDOW_SIZE, pty_window_size_t);
