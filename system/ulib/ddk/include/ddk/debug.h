// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <ddk/driver.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/log.h>

__BEGIN_CDECLS

// Log Flags

// Error messages should indicate unexpected failures.  They
// should be terse (preferably one-line) but informative.  They
// should avoid flooding the log (if an error is likely to happen
// repeatedly, rapidly, it should throttle its dprintf()s).
// Error messages are always displayed by default.
#define DDK_LOG_ERROR    MX_LOG_ERROR

// Info messages should provide terse information messages
// around driver startup, shutdown or state change.  They
// should be concise, infrequent, and one-line whenever possible.
// Info messages are always displayed by default.
#define DDK_LOG_INFO     MX_LOG_INFO

// Trace messages are intended to provide detailed information
// about what a driver is doing (start/end of transaction, etc)
// They should aim for terseness, but provide visibility into
// driver operation.  They are not displayed by default.
#define DDK_LOG_TRACE    MX_LOG_TRACE

// Spew messages are extremely verbose driver state tracing
// (possibly including register dumps / full state dumps).
// They are not displayed by default.
#define DDK_LOG_SPEW     MX_LOG_SPEW

// Debug1 through Debug4 messages are driver specific, and not
// displayed by default.  Consult driver source or documentation
// to learn if these messages exist for a specific driver and
// what they're used for.
#define DDK_LOG_DEBUG1   MX_LOG_DEBUG1
#define DDK_LOG_DEBUG2   MX_LOG_DEBUG2
#define DDK_LOG_DEBUG3   MX_LOG_DEBUG3
#define DDK_LOG_DEBUG4   MX_LOG_DEBUG4


// Local variants of log levels.  These levels will flag debug
// messages so they do not get sent over the network.  They're
// useful for network core or driver logging that would otherwise
// spiral out of control as it logs about packets about logging...
#define DDK_LOG_LERROR   (MX_LOG_ERROR | MX_LOG_LOCAL)
#define DDK_LOG_LINFO    (MX_LOG_INFO | MX_LOG_LOCAL)
#define DDK_LOG_LTRACE   (MX_LOG_TRACE | MX_LOG_LOCAL)
#define DDK_LOG_LSPEW    (MX_LOG_SPEW | MX_LOG_LOCAL)
#define DDK_LOG_LDEBUG1  (MX_LOG_DEBUG1 | MX_LOG_LOCAL)
#define DDK_LOG_LDEBUG2  (MX_LOG_DEBUG2 | MX_LOG_LOCAL)
#define DDK_LOG_LDEBUG3  (MX_LOG_DEBUG3 | MX_LOG_LOCAL)
#define DDK_LOG_LDEBUG4  (MX_LOG_DEBUG4 | MX_LOG_LOCAL)


void driver_printf(uint32_t flags, const char* fmt, ...);

// dprintf() provides a path to the kernel debuglog gated by log level flags
//
// Example:  dprintf(ERROR, "oh no! ...");
//
// By default drivers have ERROR and INFO debug levels enabled.
// The kernel commandline option driver.NAME.log may be used to override
// this.  Its value is a comma-separated list of log levels to enable (prefixed
// with '+') or disable (prefixed with '-').  The levels are the strings
// "error", "info", "trace", "spew", "debug1", "debug2", "debug3", and "debug4",
// or an integer mask in decimal, octal, or hex.
//
// Example driver.floppydisk.log=-info,+trace,+0x10
//
#define dprintf(flag, fmt...) \
    do { \
        if ((DDK_LOG_##flag & MX_LOG_LEVEL_MASK) & __magenta_driver_rec__.log_flags) { \
            driver_printf(DDK_LOG_##flag, fmt); \
        } \
    } while (0)

static inline void driver_set_log_flags(uint32_t flags) {
    __magenta_driver_rec__.log_flags = flags;
}

static inline uint32_t driver_get_log_flags(void) {
    return __magenta_driver_rec__.log_flags;
}

__END_CDECLS
