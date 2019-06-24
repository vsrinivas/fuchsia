// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-buffer-collection.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/syslog/global.h>
#include <lib/zx/vmo.h>
#include <src/camera/drivers/isp/modules/dma-format.h>
#include <stdlib.h>
#include <unistd.h>

namespace camera {

zx_status_t CreateContiguousBufferCollectionInfo(
    fuchsia_sysmem_BufferCollectionInfo* buffer_collection,
    zx_handle_t bti_handle, uint32_t width, uint32_t height,
    uint32_t num_buffers) {
  // set all the vmo handles to invalid:
  for (uint32_t i = 0; i < countof(buffer_collection->vmos); ++i) {
    buffer_collection->vmos[i] = ZX_HANDLE_INVALID;
  }
  buffer_collection->format.image = {
      .width = width,
      .height = height,
      .layers = 2u,
      .pixel_format =
          {
              .type = fuchsia_sysmem_PixelFormatType_NV12,
              .has_format_modifier = false,
              .format_modifier = {.value = 0},
          },
      .color_space =
          {
              .type = fuchsia_sysmem_ColorSpaceType_SRGB,
          },
      // The planes data is not used currently:
      .planes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}}};
  buffer_collection->buffer_count = num_buffers;
  // Get the image size for the vmo:
  DmaFormat full_res_format(buffer_collection->format.image);
  buffer_collection->vmo_size = full_res_format.GetImageSize();
  zx_status_t status;
  for (uint32_t i = 0; i < buffer_collection->buffer_count; ++i) {
    status = zx_vmo_create_contiguous(bti_handle, buffer_collection->vmo_size,
                                      0, &buffer_collection->vmos[i]);
    if (status != ZX_OK) {
      FX_LOG(ERROR, "", "Failed to allocate Buffer Collection");
      return status;
    }
  }
  return ZX_OK;
}
}  // namespace camera
