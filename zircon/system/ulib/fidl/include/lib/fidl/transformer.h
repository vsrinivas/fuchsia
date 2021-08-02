// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_TRANSFORMER_H_
#define LIB_FIDL_TRANSFORMER_H_

#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef uint32_t fidl_transformation_t;

#define FIDL_TRANSFORMATION_NONE ((fidl_transformation_t)0u)
#define FIDL_TRANSFORMATION_V1_TO_V2 ((fidl_transformation_t)1u)
#define FIDL_TRANSFORMATION_V2_TO_V1 ((fidl_transformation_t)2u)

// internal__fidl_transform__may_break converts bytes from one version of the FIDL wire format
// to another.
//
// It is intended to be used short-term to facilitate migrations and
// MAY CHANGE OR BREAK AT ANY TIME WITHOUT NOTICE.
//
// |transformation| indicates the type of transformation to perform.
//
// |type| describes the type of both the source and destination objects.
//
// Upon success, this function returns `ZX_OK` and records the total size
// of bytes written to the |dst_bytes| buffer into |out_dst_num_bytes|.
//
// Upon failure (and if provided) this function writes an error message
// to |out_error_msg|. The caller is not responsible for the memory backing the
// error message.
zx_status_t internal__fidl_transform__may_break(fidl_transformation_t transformation,
                                                const fidl_type_t* type, const uint8_t* src_bytes,
                                                uint32_t src_num_bytes, uint8_t* dst_bytes,
                                                uint32_t dst_num_bytes_capacity,
                                                uint32_t* out_dst_num_bytes,
                                                const char** out_error_msg);

__END_CDECLS

#endif  // LIB_FIDL_TRANSFORMER_H_
