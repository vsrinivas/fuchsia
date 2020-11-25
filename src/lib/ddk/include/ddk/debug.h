// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_DEBUG_H_
#define SRC_LIB_DDK_INCLUDE_DDK_DEBUG_H_

#include <lib/syslog/logger.h>
#include <stdarg.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <ddk/driver.h>

__BEGIN_CDECLS

// Log Flags

// Error messages should indicate unexpected failures.  They
// should be terse (preferably one-line) but informative.  They
// should avoid flooding the log (if an error is likely to happen
// repeatedly, rapidly, it should throttle its dprintf()s).
// Error messages are always displayed by default.
#define DDK_LOG_ERROR FX_LOG_ERROR

// Warning messages are for situations that are not errors but
// may be indicative of an impending problem.  As with errors they
// should not be issued repeatedly and rapidly.
// Warning messages are always displayed by default.
#define DDK_LOG_WARNING FX_LOG_WARNING

// Info messages should provide terse information messages
// around driver startup, shutdown or state change.  They
// should be concise, infrequent, and one-line whenever possible.
// Info messages are always displayed by default.
#define DDK_LOG_INFO FX_LOG_INFO

// Debug messages are intended to provide detailed information
// about what a driver is doing (start/end of transaction, etc)
// They should aim for terseness, but provide visibility into
// driver operation.  They are not displayed by default.
#define DDK_LOG_DEBUG FX_LOG_DEBUG

// Trace messages are extremely verbose driver state tracing
// (possibly including register dumps / full state dumps).
// They are not displayed by default.
#define DDK_LOG_TRACE FX_LOG_TRACE

// Serial messages are intended for low-level debugging, and
// should always be written to debuglog. They are not displayed
// by default.
#define DDK_LOG_SERIAL INT32_MIN

// Do not use this function directly, use zxlog_level_enabled() instead.
bool driver_log_severity_enabled_internal(const zx_driver_t* drv, fx_log_severity_t flag);

// Do not use this macro directly, use zxlog_level_enabled() instead.
#define zxlog_level_enabled_etc(flag) \
  driver_log_severity_enabled_internal(__zircon_driver_rec__.driver, flag)

// zxlog_level_enabled() provides a way for a driver to test to see if a
// particular log level is currently enabled.  This allows for patterns where a
// driver might want to log something at debug or trace level, but the something
// that they want to log might involve a computation or for loop which cannot be
// embedded into the log macro and therefor disabled without cost.
//
// Example:
// if (zxlog_level_enabled(DEBUG)) {
//     zxlogf(DEBUG, "Scatter gather table has %u entries", sg_table.count);
//     for (uint32_t i = 0; i < sg_table.count; ++i) {
//         zxlogf(DEBUG, "[%u] : 0x%08x, %u",
//                i, sg_table.entry[i].base, sg_table.entry[i].base);
//     }
// }
#define zxlog_level_enabled(flag) zxlog_level_enabled_etc(DDK_LOG_##flag)

// Do not use this function directly, use zxlogf() instead.
void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag, const char* file,
                          int line, const char* msg, ...) __PRINTFLIKE(5, 6);

// Do not use this function directly, use zxlogvf() instead.
void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t flag, const char* file,
                           int line, const char* msg, va_list args);

// Do not use this macro directly, use zxlogf() instead.
#define zxlogf_etc(flag, msg...)                                                         \
  do {                                                                                   \
    if (driver_log_severity_enabled_internal(__zircon_driver_rec__.driver, flag)) {      \
      driver_logf_internal(__zircon_driver_rec__.driver, flag, __FILE__, __LINE__, msg); \
    }                                                                                    \
  } while (0)

// Do not use this macro directly, use zxlogvf() instead.
#define zxlogvf_etc(flag, file, line, format, args)                                                \
  do {                                                                                             \
    if (driver_log_severity_enabled_internal(__zircon_driver_rec__.driver, flag)) {                \
      driver_logvf_internal(__zircon_driver_rec__.driver, flag, file, line, format, args); \
    }                                                                                              \
  } while (0)

// zxlogf() provides a path to the kernel debuglog gated by log level flags
//
// Example:  zxlogf(ERROR, "oh no! ...");
//
// By default drivers have ERROR, WARN, and INFO debug levels enabled.
// The kernel commandline option driver.NAME.log may be used to override
// this.  NAME is specified via ZIRCON_DRIVER_BEGIN/ZIRCON_DRIVER_END
// macros on each driver's definition.  The levels are the strings "error",
// "warning", "info", "debug", or "trace".
//
// Example driver.floppydisk.log=trace
//
#define zxlogf(flag, msg...) zxlogf_etc(DDK_LOG_##flag, msg)

// zxlogvf() is similar to zxlogf() but it accepts a va_list as argument,
// analogous to vprintf() and printf().
//
// Examples:
//   void log(const char* file, const char* line, const char* format, ...) {
//     std::string new_format = std::string("[tag] ") + format;
//
//     va_list args;
//     va_start(args, format);
//     zxlogvf(file, line, new_format.c_str(), args);
//     va_end(args);
//   }
//
// The debug levels are the same as those of zxlogf() macro.
//
#define zxlogvf(flag, file, line, format, args) \
  zxlogvf_etc(DDK_LOG_##flag, file, line, format, args);

__END_CDECLS

#endif  // SRC_LIB_DDK_INCLUDE_DDK_DEBUG_H_
