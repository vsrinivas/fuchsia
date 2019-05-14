// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_HANDLE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_HANDLE_H_

//
//
//

#include "spinel_types.h"

//
// Add defensive high guard-bit flags to the opaque path and raster
// handles. This is tested once and stripped down to a handle.
//
//   union spn_typed_handle
//   {
//     spn_uint   u32;
//
//     struct {
//       spn_uint handle    : 30;
//       spn_uint is_path   :  1;
//       spn_uint is_raster :  1;
//     };
//     struct {
//       spn_uint na        : 30;
//       spn_uint type      :  2;
//     };
//   }
//

typedef uint32_t spn_typed_handle_t;
typedef uint32_t spn_handle_t;

//
// clang-format off
//

typedef enum spn_typed_handle_type_e
{
  SPN_TYPED_HANDLE_TYPE_PATH   =           0x40000000,
  SPN_TYPED_HANDLE_TYPE_RASTER = (int32_t)(0x80000000)

} spn_typed_handle_type_e;

//
//
//

#define SPN_TYPED_HANDLE_TYPE_MASK       (SPN_TYPED_HANDLE_TYPE_PATH | SPN_TYPED_HANDLE_TYPE_RASTER)

#define SPN_TYPED_HANDLE_TO_HANDLE(h)    ((h) & ~SPN_TYPED_HANDLE_TYPE_MASK)

#define SPN_TYPED_HANDLE_IS_TYPE(h,t)    (((h) & (t)) != 0)
#define SPN_TYPED_HANDLE_IS_PATH(h)      SPN_TYPE_HANDLE_IS_TYPE(SPN_TYPED_HANDLE_TYPE_PATH)
#define SPN_TYPED_HANDLE_IS_RASTER(h)    SPN_TYPE_HANDLE_IS_TYPE(SPN_TYPED_HANDLE_TYPE_RASTER)

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_HANDLE_H_
