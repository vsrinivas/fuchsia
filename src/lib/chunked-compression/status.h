// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_STATUS_H_
#define SRC_LIB_CHUNKED_COMPRESSION_STATUS_H_

#include <stdint.h>
#include <zircon/types.h>

namespace chunked_compression {

// Status is a status code which is byte-compatible with zx_status_t.
// This is locally defined so that this library can be used in both host and Fuchsia code.
//
// Only a subset of ZX_ERR_* codes are defined here. More can be added as needed.
using Status = int32_t;

// Equivalent to ZX_OK
constexpr Status kStatusOk = 0;

// Equivalent to ZX_ERR_INTERNAL
// The system encountered an otherwise unspecified error while performing the operation.
constexpr Status kStatusErrInternal = -1;

// Equivalent to ZX_ERR_INVALID_ARGS
// An argument is invalid, ex. null pointer
constexpr Status kStatusErrInvalidArgs = -10;

// Equivalent to ZX_ERR_BUFFER_TOO_SMALL
// A caller provided buffer is too small for this operation.
constexpr Status kStatusErrBufferTooSmall = -15;

// Equivalent to ZX_ERR_BAD_STATE
// Operation failed because the current state of the object does not allow it, or a precondition of
// the operation is not satisfied
constexpr Status kStatusErrBadState = -20;

// Equivalent to ZX_ERR_IO_DATA_INTEGRITY
// The data in the operation failed an integrity check and is possibly corrupted.
// Example: CRC or Parity error.
constexpr Status kStatusErrIoDataIntegrity = -42;

constexpr zx_status_t ToZxStatus(Status status) { return static_cast<zx_status_t>(status); }

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_STATUS_H_
