// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOLD_H
#define MOLD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "spinel/spinel_types.h"

typedef enum mold_pixel_format {
  MOLD_RGBA8888,
  MOLD_BGRA8888,
  MOLD_RGB565,
} mold_pixel_format;

typedef struct mold_raw_buffer {
  void** buffer_ptr;
  size_t stride;
  mold_pixel_format format;
} mold_raw_buffer;

void mold_context_create(spn_context_t* context,
                         const mold_raw_buffer* raw_buffer);

#ifdef __cplusplus
}
#endif

#endif //MOLD_H
