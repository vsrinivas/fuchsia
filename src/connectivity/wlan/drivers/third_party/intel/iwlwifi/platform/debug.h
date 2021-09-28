// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_DEBUG_H_

// This file contains Fuchsia-specific debug compilation support.

#include <fuchsia/hardware/wlan/mac/c/banjo.h>
#include <lib/ddk/debug.h>
#include <stddef.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

struct device;

// NEEDS_TYPES
#define WARN(cond, y, z...) (!!(cond))
#define WARN_ON(x) (!!(x))
#define WARN_ON_ONCE(x) (!!(x))
#define BUILD_BUG_ON(x) ZX_ASSERT(!(x))

#define iwl_assert_lock_held(x) ZX_DEBUG_ASSERT(mtx_trylock(x) == thrd_busy)

// Print a message to the kernel, of different severities. Removes the trailing newline that is
// unnecessary for zxlogf() and creates blank lines in the logs. It's a bit ugly, but it allows
// us to reduce the diffs between our code and the original driver code. If that is no longer a
// concern at some point in the future, we can remove the newlines in the format strings and
// this extra step. This potential future improvement is tracked by fxbug.dev/80797.
#define IWL_ZXLOGF(level, fmt, args...)    \
  do {                                     \
    char* buf;                             \
    size_t buf_len;                        \
    buf_len = asprintf(&buf, fmt, ##args); \
    ZX_ASSERT(buf_len > 0 && buf);         \
    if (buf[buf_len - 1] == '\n') {        \
      buf[buf_len - 1] = '\0';             \
    }                                      \
    zxlogf(level, "%s", buf);              \
    free(buf);                             \
  } while (0)
#define __iwl_err(dev, rfkill_prefix, only_trace, fmt, args...) IWL_ZXLOGF(ERROR, fmt, ##args)
#define __iwl_warn(dev, fmt, args...) IWL_ZXLOGF(WARNING, fmt, ##args)
#define __iwl_info(dev, fmt, args...) IWL_ZXLOGF(INFO, fmt, ##args)
#define __iwl_crit(dev, fmt, args...) IWL_ZXLOGF(ERROR, fmt, ##args)
#define __iwl_dbg(dev, level, limit, function, fmt, args...) \
  IWL_ZXLOGF(TRACE, "(%s): " fmt, function, ##args)

// Publish a core dump.
zx_status_t iwl_debug_core_dump(struct device* dev, const char* core_dump_name,
                                const char** core_dump_ptrs, size_t* core_dump_sizes,
                                size_t core_dump_count);

// Hex dump function
//
// This function will dump the specified memory range with hex values and ASCII chars.
//
// - hex_dump(): the actual function printing the output.
//
//   Args:
//     prefix: a string outputting before each line.
//     ptr: the starting memory address to dump.
//     len: the length to dump (in bytes).
//
// - hex_dump_str(): for testing, returns one line only.
//
//   Args:
//     output: a output buffer larger than or equal to HEX_DUMP_BUF_SIZE bytes.
//     ptr: the starting memory address to dump.
//     len: the length to dump (in bytes).
//
//   Returns:
//     The output buffer given in the input argument. This is helpful in printf("%s").
//     NULL if the output buffer is too small.
//
static const char kNP = '.';  // the character used to show non-printable character.
void hex_dump(const char* prefix, const void* ptr, size_t len);
// for testing
#define HEX_DUMP_BUF_SIZE 70
char* hex_dump_str(char* output, size_t output_size, const void* ptr, size_t len);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_DEBUG_H_
