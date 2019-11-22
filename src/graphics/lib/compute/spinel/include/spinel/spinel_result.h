// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_RESULT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_RESULT_H_

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// FIXME(allanmac)
//
//  - add missing error codes for incomplete stages in pipeline
//
//  - remap or harvest OpenCL-era error codes
//
//  - consider platform-specific error codes to Spinel error codes
//    (see previous implementations)
//

#define SPN_RESULTS()                                                                              \
                                                                                                   \
  SPN_RESULT(SPN_SUCCESS)                                                                          \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS)                                                \
                                                                                                   \
  SPN_RESULT(SPN_TIMEOUT)                                                                          \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_NOT_IMPLEMENTED)                                                            \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_CONTEXT_LOST)                                                               \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_PATH_BUILDER_LOST)                                                          \
  SPN_RESULT(SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN)                                                \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_RASTER_BUILDER_LOST)                                                        \
  SPN_RESULT(SPN_ERROR_RASTER_BUILDER_SEALED)                                                      \
  SPN_RESULT(SPN_ERROR_RASTER_BUILDER_TOO_MANY_PATHS)                                              \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_RENDER_EXTENSION_INVALID)                                                   \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_LAYER_ID_INVALID)                                                           \
  SPN_RESULT(SPN_ERROR_LAYER_NOT_EMPTY)                                                            \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_POOL_EMPTY)                                                                 \
  SPN_RESULT(SPN_ERROR_CONDVAR_WAIT)                                                               \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_TRANSFORM_WEAKREF_INVALID)                                                  \
  SPN_RESULT(SPN_ERROR_STROKE_STYLE_WEAKREF_INVALID)                                               \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_COMMAND_NOT_READY)                                                          \
  SPN_RESULT(SPN_ERROR_COMMAND_NOT_COMPLETED)                                                      \
  SPN_RESULT(SPN_ERROR_COMMAND_NOT_STARTED)                                                        \
  SPN_RESULT(SPN_ERROR_COMMAND_NOT_READY_OR_COMPLETED)                                             \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_COMPOSITION_SEALED)                                                         \
  SPN_RESULT(SPN_ERROR_COMPOSITION_TOO_MANY_RASTERS)                                               \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_STYLING_SEALED)                                                             \
                                                                                                   \
  SPN_RESULT(SPN_ERROR_HANDLE_INVALID)                                                             \
  SPN_RESULT(SPN_ERROR_HANDLE_OVERFLOW)

//
//
//

typedef enum spn_result_t
{
#undef SPN_RESULT
#define SPN_RESULT(res_) res_,

  SPN_RESULTS()

} spn_result_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_RESULT_H_
