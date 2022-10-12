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

#ifndef SRC_FIRMWARE_LIB_ZBI_INCLUDE_LIB_ZBI_ZBI_H_
#define SRC_FIRMWARE_LIB_ZBI_INCLUDE_LIB_ZBI_ZBI_H_

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
} zbi_result_t;

typedef zbi_result_t (*zbi_foreach_cb_t)(zbi_header_t* hdr, void* payload, void* cookie);

// Creates an empty ZBI container in the buffer.
//
// The buffer must be aligned to ZBI_ALIGNMENT and large enough to store the
// empty container.
//
// Parameters:
//     buffer - The buffer the container will be created in.
//     length - The size of the buffer.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If buffer is NULL.
//     ZBI_RESULT_TOO_BIG - If the container cannot fit in the buffer.
//     ZBI_RESULT_BAD_ALIGNMENT - If the buffer is not aligned.
zbi_result_t zbi_init(void* buffer, size_t length);

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

// Validates the ZBI for the host platform.
//
// Same as zbi_check but also diagnoses ZBI_RESULT_INCOMPLETE_* result codes
// if the ZBI is not a valid, bootable ZBI for the host platform.
//
// Parameters:
//     base - The ZBI to check.
//     err - Optional, set to the problem entry if one is found.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base is NULL.
//     Not ZBI_RESULT_OK - Indicating the error.
zbi_result_t zbi_check_bootable(const void* base, zbi_header_t** err);

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
//     payload_length - The length of the new entry's payload.
//     payload - Set to the address of the entry's payload. May be NULL if
//               |payload_length| is 0 or the payload has been previously
//               filled via zbi_get_next_entry_payload().
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base is NULL or if the CRC32 flag is used.
//     ZBI_RESULT_BAD_TYPE - If the base ZBI is not a valid ZBI container.
//     ZBI_RESULT_TOO_BIG - If the base ZBI is too small.
zbi_result_t zbi_create_entry(void* base, size_t capacity, uint32_t type, uint32_t extra,
                              uint32_t flags, size_t payload_length, void** payload);

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
//     payload_length - The length of the new entry's payload.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If base or payload is NULL or if the CRC32 flag is used.
//     ZBI_RESULT_BAD_TYPE - If the base ZBI is not a valid ZBI container.
//     ZBI_RESULT_TOO_BIG - If the base ZBI is too small.
zbi_result_t zbi_create_entry_with_payload(void* base, size_t capacity, uint32_t type,
                                           uint32_t extra, uint32_t flags, const void* payload,
                                           size_t payload_length);

// Returns the payload buffer for the next ZBI entry to add.
//
// This is useful when it's non-trivial to determine the length of a payload
// ahead of time - for example, loading a variable-length string from persistent
// storage.
//
// Rather than loading the payload into a temporary buffer, determining the
// length, then copying it into the ZBI, this function allows loading data
// directly into the ZBI. Since this buffer is currently unused area, loading
// data here does not affect the ZBI until zbi_create_entry() is called.
//
// Expected usage:
//   1. Get payload buffer and max size from zbi_get_next_entry_payload()
//   2. Fill payload with data
//   3. Call zbi_create_entry() to add the new ZBI entry to the container.
//
// Parameters:
//     base - The base ZBI.
//     capacity - The max potential size of the base ZBI.
//     payload - Set to the address of the next entry's payload.
//     max_payload_length - Set to the max length the next payload can have.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If required args are NULL or the CRC32 flag is used.
//     ZBI_RESULT_BAD_TYPE - If the base ZBI is not a valid ZBI container.
//     ZBI_RESULT_TOO_BIG - If the ZBI capacity is too small to add a new item.
zbi_result_t zbi_get_next_entry_payload(void* base, size_t capacity, void** payload,
                                        uint32_t* max_payload_length);

// Extends a ZBI container with another container's payload.
//
// Both dst and src must be ZBI containers.
//
// Parameters:
//     dst - The destination container.
//     capacity - The max potential size of the base ZBI.
//     src - The container to copy the payload from.
//
// Returns:
//     ZBI_RESULT_OK - On success.
//     ZBI_RESULT_ERROR - If dst or src is NULL.
//     ZBI_RESULT_BAD_TYPE - If dst or src is not a container.
//     ZBI_RESULT_TOO_BIG - If dst is too small.
zbi_result_t zbi_extend(void* dst, size_t capacity, const void* src);

__END_CDECLS

#endif  // SRC_FIRMWARE_LIB_ZBI_INCLUDE_LIB_ZBI_ZBI_H_
