// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <ddk/driver.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// Log Flags

// Error messages should indicate unexpected failures.  They
// should be terse (preferably one-line) but informative.  They
// should avoid flooding the log (if an error is likely to happen
// repeatedly, rapidly, it should throttle its dprintf()s).
// Error messages are always displayed by default.
#define DDK_LOG_ERROR  (0x0001)

// Info messages should provide terse information messages
// around driver startup, shutdown or state change.  They
// should be concise, infrequent, and one-line whenever possible.
// Info messages are always displayed by default.
#define DDK_LOG_INFO   (0x0002)

// Trace messages are intended to provide detailed information
// about what a driver is doing (start/end of transaction, etc)
// They should aim for terseness, but provide visibility into
// driver operation.  They are not displayed by default.
#define DDK_LOG_TRACE  (0x0004)

// Spew messages are extremely verbose driver state tracing
// (possibly including register dumps / full state dumps).
// They are not displayed by default.
#define DDK_LOG_SPEW   (0x0008)

// Debug1 through Debug4 messages are driver specific, and not
// displayed by default.  Consult driver source or documentation
// to learn if these messages exist for a specific driver and
// what they're used for.
#define DDK_LOG_DEBUG1 (0x0010)
#define DDK_LOG_DEBUG2 (0x0020)
#define DDK_LOG_DEBUG3 (0x0040)
#define DDK_LOG_DEBUG4 (0x0080)


void driver_printf(const char* fmt, ...);

// dprintf() provides a path to the kernel debuglog gated by log level flags
//
// Example:  dprintf(ERROR, "oh no! ...");
//
// By default drivers have ERROR and INFO debug levels enabled.
// The kernel commandline option driver.NAME.log may be used to override
// this.  Its value is one or more numeric values or words "error", "info",
// "trace", "spew", separated by ':', which will be ORed together and the
// log level set to that.
//
// Example driver.floppydisk.log=error:info:trace
// Example driver.floppydisk.log=error:0x178
//
#define dprintf(flag, fmt...) \
    do { \
        if (DDK_LOG_##flag & __magenta_driver_rec__.log_flags) { \
            driver_printf(fmt); \
        } \
    } while (0)

static inline void driver_set_log_flags(uint32_t flags) {
    __magenta_driver_rec__.log_flags = flags;
}

static inline uint32_t driver_get_log_flags(void) {
    return __magenta_driver_rec__.log_flags;
}

__END_CDECLS
