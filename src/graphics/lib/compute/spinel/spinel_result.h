// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_RESULT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_RESULT_H_

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// FIXME -- harvest error codes that are no longer used
//

#define SPN_RESULT_EXPAND()                                                                        \
  SPN_RESULT_EXPAND_X(SPN_SUCCESS)                                                                 \
  SPN_RESULT_EXPAND_X(SPN_ERROR_NOT_IMPLEMENTED)                                                   \
  SPN_RESULT_EXPAND_X(SPN_ERROR_CONTEXT_LOST)                                                      \
  SPN_RESULT_EXPAND_X(SPN_ERROR_PATH_BUILDER_LOST)                                                 \
  SPN_RESULT_EXPAND_X(SPN_ERROR_RASTER_BUILDER_LOST)                                               \
  SPN_RESULT_EXPAND_X(SPN_ERROR_RASTER_BUILDER_SEALED)                                             \
  SPN_RESULT_EXPAND_X(SPN_ERROR_RASTER_BUILDER_TOO_MANY_RASTERS)                                   \
  SPN_RESULT_EXPAND_X(SPN_ERROR_RENDER_EXTENSION_INVALID)                                          \
  SPN_RESULT_EXPAND_X(SPN_ERROR_RENDER_EXTENSION_VK_SUBMIT_INFO_WAIT_COUNT_EXCEEDED)               \
  SPN_RESULT_EXPAND_X(SPN_ERROR_LAYER_ID_INVALID)                                                  \
  SPN_RESULT_EXPAND_X(SPN_ERROR_LAYER_NOT_EMPTY)                                                   \
  SPN_RESULT_EXPAND_X(SPN_ERROR_POOL_EMPTY)                                                        \
  SPN_RESULT_EXPAND_X(SPN_ERROR_CONDVAR_WAIT)                                                      \
  SPN_RESULT_EXPAND_X(SPN_ERROR_TRANSFORM_WEAKREF_INVALID)                                         \
  SPN_RESULT_EXPAND_X(SPN_ERROR_STROKE_STYLE_WEAKREF_INVALID)                                      \
  SPN_RESULT_EXPAND_X(SPN_ERROR_COMMAND_NOT_READY)                                                 \
  SPN_RESULT_EXPAND_X(SPN_ERROR_COMMAND_NOT_COMPLETED)                                             \
  SPN_RESULT_EXPAND_X(SPN_ERROR_COMMAND_NOT_STARTED)                                               \
  SPN_RESULT_EXPAND_X(SPN_ERROR_COMMAND_NOT_READY_OR_COMPLETED)                                    \
  SPN_RESULT_EXPAND_X(SPN_ERROR_COMPOSITION_SEALED)                                                \
  SPN_RESULT_EXPAND_X(SPN_ERROR_STYLING_SEALED)                                                    \
  SPN_RESULT_EXPAND_X(SPN_ERROR_HANDLE_INVALID)                                                    \
  SPN_RESULT_EXPAND_X(SPN_ERROR_HANDLE_OVERFLOW)

//
//
//

typedef enum spn_result
{

#undef SPN_RESULT_EXPAND_X
#define SPN_RESULT_EXPAND_X(_res) _res,

  SPN_RESULT_EXPAND()

} spn_result;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_RESULT_H_
