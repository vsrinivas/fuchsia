// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_DEBUG_H_

// This file contains Fuchsia-specific debug compilation support.

#include <lib/ddk/debug.h>
#include <stddef.h>
#include <zircon/assert.h>

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

// NEEDS_TYPES
#define WARN(x, y, z...) \
  do {                   \
  } while (0)
#define WARN_ON(x) (!!(x))
#define WARN_ON_ONCE(x) (!!(x))
#define BUILD_BUG_ON(x) ZX_ASSERT(!(x))

#define iwl_assert_lock_held(x) ZX_DEBUG_ASSERT(mtx_trylock(x) == thrd_busy)

// Print a message to the kernel, of different severities.
#define __iwl_err(dev, rfkill_prefix, only_trace, fmt, ...) \
  zxlogf(ERROR, "iwlwifi: " fmt, ##__VA_ARGS__)
#define __iwl_warn(dev, fmt, args...) zxlogf(WARNING, "iwlwifi: " fmt, ##args)
#define __iwl_info(dev, fmt, args...) zxlogf(INFO, "iwlwifi: " fmt, ##args)
#define __iwl_crit(dev, fmt, args...) zxlogf(ERROR, "iwlwifi: " fmt, ##args)
#define __iwl_dbg(dev, level, limit, function, fmt, args...) \
  zxlogf(TRACE, "iwlwifi (%s): " fmt, function, ##args)

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
