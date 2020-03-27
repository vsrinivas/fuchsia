// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_TRANSFORMER_H_
#define LIB_FIDL_TRANSFORMER_H_

#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Available transformations.
typedef uint32_t fidl_transformation_t;

// No-op transformation.
//
// See also `fidl_transform`.
#define FIDL_TRANSFORMATION_NONE ((fidl_transformation_t)0u)

// In the old wire format, a FIDL union is encoded as a static union. In the v1 wire format, a FIDL
// union is encoded as an extensible union.
//
// Performing the FIDL_TRANSFORMATION_V1_TO_OLD transformation will transform a top-level struct
// that contains FIDL unions from extensible unions to static unions. The |src_bytes| buffer passed
// to `fidl_transform` MUST have been previously validated with `fidl_validate`, or the behavior is
// undefined.
//
// See also `fidl_transform`.
#define FIDL_TRANSFORMATION_V1_TO_OLD ((fidl_transformation_t)1u)

// Performing FIDL_TRANSFORMATION_OLD_TO_V1 transformation will transform a top-level struct that
// contains FIDL unions from static unions to extensible unions. The |src_bytes| buffer passed to
// `fidl_transform` MUST have been previously validated with `fidl_validate`, or the behavior is
// undefined.
//
// See also `fidl_transform`.
#define FIDL_TRANSFORMATION_OLD_TO_V1 ((fidl_transformation_t)2u)

// Transforms an encoded FIDL buffer from one wire format to another.
//
// Starting from the root of the encoded objects present in the |src_bytes|
// buffer, this function traverses all objects and transforms them from one
// wire format into another, placing the transformed encoded objects into the
// |dst_bytes| buffer. Both |src_bytes| and |dst_bytes| should be aligned to
// FIDL_ALIGNMENT.
//
// The provided |src_type| must describe the |src_bytes| buffer format, and the
// alternate types (accessed via the various `alt_type` fields) must describe
// the target buffer format.
//
// Upon success, this function returns `ZX_OK` and records the total size
// of bytes written to the |dst_bytes| buffer into |out_dst_num_bytes|.
//
// Upon failure (and if provided) this function writes an error message
// to |out_error_msg|. The caller is not responsible for the memory backing the
// error message.
//
// See also `fidl_transformation_t` and `FIDL_TRANSFORMATION_...` constants.
zx_status_t fidl_transform(fidl_transformation_t transformation, const fidl_type_t* src_type,
                           const uint8_t* src_bytes, uint32_t src_num_bytes, uint8_t* dst_bytes,
                           uint32_t dst_num_bytes_capacity,  uint32_t* out_dst_num_bytes,
                           const char** out_error_msg);

__END_CDECLS

#endif  // LIB_FIDL_TRANSFORMER_H_
