// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/log.h>

__BEGIN_CDECLS

// Log Flags

// Error messages should indicate unexpected failures.  They
// should be terse (preferably one-line) but informative.  They
// should avoid flooding the log (if an error is likely to happen
// repeatedly, rapidly, it should throttle its dprintf()s).
// Error messages are always displayed by default.
#define DDK_LOG_ERROR    ZX_LOG_ERROR

// Warning messages are for situations that are not errors but
// may be indicative of an impending problem.  As with errors they
// should not be issued repeatedly and rapidly.
// Warning messages are always displayed by default.
#define DDK_LOG_WARN     ZX_LOG_WARN

// Info messages should provide terse information messages
// around driver startup, shutdown or state change.  They
// should be concise, infrequent, and one-line whenever possible.
// Info messages are always displayed by default.
#define DDK_LOG_INFO     ZX_LOG_INFO

// Trace messages are intended to provide detailed information
// about what a driver is doing (start/end of transaction, etc)
// They should aim for terseness, but provide visibility into
// driver operation.  They are not displayed by default.
#define DDK_LOG_TRACE    ZX_LOG_TRACE

// Spew messages are extremely verbose driver state tracing
// (possibly including register dumps / full state dumps).
// They are not displayed by default.
#define DDK_LOG_SPEW     ZX_LOG_SPEW

// Debug1 through Debug4 messages are driver specific, and not
// displayed by default.  Consult driver source or documentation
// to learn if these messages exist for a specific driver and
// what they're used for.
#define DDK_LOG_DEBUG1   ZX_LOG_DEBUG1
#define DDK_LOG_DEBUG2   ZX_LOG_DEBUG2
#define DDK_LOG_DEBUG3   ZX_LOG_DEBUG3
#define DDK_LOG_DEBUG4   ZX_LOG_DEBUG4


// Local variants of log levels.  These levels will flag debug
// messages so they do not get sent over the network.  They're
// useful for network core or driver logging that would otherwise
// spiral out of control as it logs about packets about logging...
#define DDK_LOG_LERROR   (ZX_LOG_ERROR | ZX_LOG_LOCAL)
#define DDK_LOG_LWARN    (ZX_LOG_WARN | ZX_LOG_LOCAL)
#define DDK_LOG_LINFO    (ZX_LOG_INFO | ZX_LOG_LOCAL)
#define DDK_LOG_LTRACE   (ZX_LOG_TRACE | ZX_LOG_LOCAL)
#define DDK_LOG_LSPEW    (ZX_LOG_SPEW | ZX_LOG_LOCAL)
#define DDK_LOG_LDEBUG1  (ZX_LOG_DEBUG1 | ZX_LOG_LOCAL)
#define DDK_LOG_LDEBUG2  (ZX_LOG_DEBUG2 | ZX_LOG_LOCAL)
#define DDK_LOG_LDEBUG3  (ZX_LOG_DEBUG3 | ZX_LOG_LOCAL)
#define DDK_LOG_LDEBUG4  (ZX_LOG_DEBUG4 | ZX_LOG_LOCAL)

// zxlog_level_enabled_etc(...) is an internal macro which tests to see if a
// given log level is currently enabled.  Users should not use this macro, they
// should use zxlog_level_enabled(...) instead.
#define zxlog_level_enabled_etc(flag) \
    (((flag & ZX_LOG_LEVEL_MASK) & __zircon_driver_rec__.log_flags) != 0)

// zxlog_level_enabled(...) provides a way for a driver to test to see if a
// particular log level is currently enabled.  This allows for patterns where a
// driver might want to log something at trace or spew level, but the something
// that they want to log might involve a computation or for loop which cannot be
// embedded into the log macro and therefor disabled without cost.
//
// Example:
// if (zxlog_level_enabled(TRACE)) {
//     zxlogf(TRACE, "Scatter gather table has %u entries\n", sg_table.count);
//     for (uint32_t i = 0; i < sg_table.count; ++i) {
//         zxlogf(TRACE, "[%u] : 0x%08x, %u\n",
//                i, sg_table.entry[i].base, sg_table.entry[i].base);
//     }
// }
#define zxlog_level_enabled(flag) zxlog_level_enabled_etc(DDK_LOG_##flag)

void driver_printf(uint32_t flags, const char* fmt, ...) __PRINTFLIKE(2, 3);

// zxlogf() provides a path to the kernel debuglog gated by log level flags
//
// Example:  zxlogf(ERROR, "oh no! ...");
//
// By default drivers have ERROR, WARN, and INFO debug levels enabled.
// The kernel commandline option driver.NAME.log may be used to override
// this.  Its value is a comma-separated list of log levels to enable (prefixed
// with '+') or disable (prefixed with '-').  The levels are the strings
// "error", "info", "trace", "spew", "debug1", "debug2", "debug3", and "debug4",
// or an integer mask in decimal, octal, or hex.
//
// Example driver.floppydisk.log=-info,+trace,+0x10
//
#define zxlogf(flag, fmt...) \
    do { \
        if (zxlog_level_enabled_etc(DDK_LOG_##flag)) { \
            driver_printf(DDK_LOG_##flag, fmt); \
        } \
    } while (0)

static inline void driver_set_log_flags(uint32_t flags) {
    __zircon_driver_rec__.log_flags = flags;
}

static inline uint32_t driver_get_log_flags(void) {
    return __zircon_driver_rec__.log_flags;
}

typedef uint32_t DPRINTF_IS_DEPRECATED_USE_ZXLOGF __attribute__((deprecated));
#define dprintf(flag, fmt...) \
    do { \
        if (zxlog_level_enabled_etc(DDK_LOG_##flag)) { \
            driver_printf((DPRINTF_IS_DEPRECATED_USE_ZXLOGF)(DDK_LOG_##flag), fmt); \
        } \
    } while (0)


__END_CDECLS
