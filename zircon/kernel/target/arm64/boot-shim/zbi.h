// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ZBI Processing Library
// This library is meant to be a generic processing library for the ZBI format
// defined in system/public/zircon/boot/image.h
//
// This library has several features:
// (1) Zero allocations / Exceptions
//     Safe to use at early boot time if necessary or in other situations where
//     allocation may not be desirable.
// (2) Trivially Portable
//     This library attempts to limit the number of dependencies on the
//     environment:
//      + A C99 compliant compiler
//      + Support for sized integer types (uint8_t and uint32_t)
//      + zircon/boot/image.h
//      + Implementations of memcmp and memcpy
// (3) Tested
//     Tests for this library can be found at zircon/system/utest/zbi/*

#ifndef ZIRCON_KERNEL_TARGET_ARM64_BOOT_SHIM_ZBI_H_
#define ZIRCON_KERNEL_TARGET_ARM64_BOOT_SHIM_ZBI_H_

#include <stddef.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef enum zbi_result {
  ZBI_RESULT_OK,

  ZBI_RESULT_ERROR,
  ZBI_RESULT_BAD_TYPE,
  ZBI_RESULT_BAD_MAGIC,
  ZBI_RESULT_BAD_VERSION,
  ZBI_RESULT_BAD_CRC,
  ZBI_RESULT_BAD_ALIGNMENT,
  ZBI_RESULT_ERR_TRUNCATED,

  ZBI_RESULT_TOO_BIG,

  ZBI_RESULT_INCOMPLETE_KERNEL,
  ZBI_RESULT_INCOMPLETE_BOOTFS,
} zbi_result_t;

typedef zbi_result_t (*zbi_foreach_cb_t)(zbi_header_t* hdr, void* payload, void* cookie);

// Validates the ZBI.
//
// Checks the container and all of its entries.
//
// If an error is found and err is not null, err will point to the ZBI entry in
// which the problem was found.
//
// Parameters:
//     base - The ZBI to check.
//     err - Optional, set to the problem entry if one is found.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base is NULL.
//     Not ZBI_RESULT_OK - Indicating the error.
zbi_result_t zbi_check(const void* base, zbi_header_t** err);

// Calls the callback with a pointer to the header and payload of each ZBI
// entry (excluding the container).
//
// Returns early if the callback does not return ZBI_RESULT_OK, leaving
// previous entries in a potentially modified state.
//
// Parameters:
//     base - The ZBI to iterate over.
//     callback - The callback invoked for each entry.
//     cookie - Transparent data provided by the client to the callback.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base or the callback is NULL.
//     ZBI_RESULT_ERR_TRUNCATED - If the next entry would read past the ZBI.
//     An error returned by the callback.
zbi_result_t zbi_for_each(const void* base, const zbi_foreach_cb_t callback, void* cookie);

// Creates a new ZBI entry and returns a pointer to the payload.
//
// The new entry is aligned to ZBI_ALIGNMENT. The capacity of the base ZBI must
// be large enough to fit the new entry.
//
// The ZBI_FLAGS_VERSION is unconditionally set for the new entry.
//
// The ZBI_FLAGS_CRC32 flag yields an error because CRC computation is not yet
// supported.
//
// Parameters:
//     base - The base ZBI.
//     capacity - The max potential size of the base ZBI.
//     type - The new entry's type.
//     extra - The new entry's type-specific data.
//     flags - The new entry's flags.
//     payload_length - The length of the new entry’s payload.
//     payload - Set to the address of the entry's payload.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base or payload is NULL or if the CRC32 flag is used.
//     ZBI_RESULT_BAD_TYPE - If the base ZBI is not a valid ZBI container.
//     ZBI_RESULT_TOO_BIG - If the base ZBI is too small.
zbi_result_t zbi_create_entry(void* base, size_t capacity, uint32_t type, uint32_t extra,
                              uint32_t flags, uint32_t payload_length, void** payload);

// Creates a new ZBI entry with the provided payload.
//
// The new entry is aligned to ZBI_ALIGNMENT. The capacity of the base ZBI must
// be large enough to fit the new entry.
//
// The ZBI_FLAGS_VERSION is unconditionally set for the new entry.
//
// The ZBI_FLAGS_CRC32 flag yields an error because CRC computation is not yet
// supported.
//
// Parameters:
//     base - The base ZBI.
//     capacity - The max potential size of the base ZBI.
//     type - The new entry's type.
//     extra - The new entry's type-specific data.
//     flags - The new entry's flags.
//     payload - The payload, copied into the new entry.
//     payload_length - The length of the new entry’s payload.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base or payload is NULL or if the CRC32 flag is used.
//     ZBI_RESULT_BAD_TYPE - If the base ZBI is not a valid ZBI container.
//     ZBI_RESULT_TOO_BIG - If the base ZBI is too small.
zbi_result_t zbi_create_entry_with_payload(void* base, size_t capacity, uint32_t type,
                                           uint32_t extra, uint32_t flags, const void* payload,
                                           uint32_t payload_length);

__END_CDECLS

#endif  // ZIRCON_KERNEL_TARGET_ARM64_BOOT_SHIM_ZBI_H_
