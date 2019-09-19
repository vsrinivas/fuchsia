// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_writer.h"

#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <src/lib/fxl/logging.h>

#include "src/camera/drivers/isp/modules/dma-format.h"

namespace camera {

static constexpr bool kFlipVertical = false;
static constexpr uint32_t kBytesPerMegaPixel = 4;

std::unique_ptr<ImageWriter> ImageWriter::Create(uint32_t width, uint32_t height,
                                                 camera::DmaFormat::PixelType pixel_format) {
  ZX_ASSERT(pixel_format == camera::DmaFormat::RAW12);

  camera::DmaFormat dma_format = camera::DmaFormat(width, height, pixel_format, kFlipVertical);
  size_t size = dma_format.GetImageSize();
  ZX_ASSERT(size % kBytesPerMegaPixel == 0);

  return std::make_unique<ImageWriter>(size);
}

zx_status_t ImageWriter::CreateImage(zx::vmo* vmo) {
  zx_status_t status = zx::vmo::create(vmo_size_, 0, vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create VMO: %s\n", zx_status_get_string(status);
    return status;
  }

  FillRAW12(vmo);

  return ZX_OK;
}

void ImageWriter::FillRAW12(zx::vmo* vmo) {
  ZX_DEBUG_ASSERT(*vmo);

  // TODO(nzo): implement RAW12 image logic.
  uint32_t pixel_val = 1;
  size_t num_pixels = vmo_size_ / kBytesPerMegaPixel;
  auto buf = std::vector<uint32_t>(num_pixels, pixel_val);

  vmo->write(&buf.front(), 0, buf.size());
}

}  // namespace camera
