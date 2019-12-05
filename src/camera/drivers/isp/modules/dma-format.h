// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_FORMAT_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_FORMAT_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <stdint.h>

#include <cstddef>
#include <cstdint>

namespace camera {

// DmaFormat is a local format that is compatible with the sysmem::ImageFormat.
// DmaFormat provides a single point of conversion between sysmem and the ISP
// driver.
class DmaFormat {
 public:
  static constexpr uint8_t kPlaneSelectShift = 6;
  enum PixelType {
    INVALID = 0,
    RGB32 = 1,
    A2R10G10B10 = 2,
    RGB565 = 3,
    RGB24 = 4,
    GEN32 = 5,
    RAW16 = 6,
    RAW12 = 7,
    AYUV = 8,
    Y410 = 9,
    YUY2 = 10,
    UYVY = 11,
    Y210 = 12,
    // The NV12 and YV12 formats are only used internally.  They should not be
    // passed to DmaFormat during initialization.
    NV12 = 13,
    YV12 = 14,
    // The types below are variants of NV12 and YV12 used to specify the
    // configuration of the UV planes.  These formats should be used in the
    // constructor.
    NV12_YUV = NV12 | (1 << kPlaneSelectShift),
    NV12_YVU = NV12 | (2 << kPlaneSelectShift),
    NV12_GREY = NV12 | (3 << kPlaneSelectShift),
    YV12_YU = YV12 | (1 << kPlaneSelectShift),
    YV12_YV = YV12 | (2 << kPlaneSelectShift),
  };

  explicit DmaFormat(const fuchsia_sysmem_ImageFormat& format);
  explicit DmaFormat(const fuchsia_sysmem_ImageFormat_2& format);
  DmaFormat(uint32_t width, uint32_t height, PixelType pixel_format, bool flip_vertical);

  // Indicates whether the format produces a second plane of output.
  bool HasSecondaryChannel() const;

  uint32_t GetBytesPerPixel() const;
  // Gets the value that should be written into the line_offset register.
  // This value corresponds to the stride.
  // Note that the register expects a negative value if the frame is vertically
  // flipped.
  uint32_t GetLineOffset() const;

  // Gets the offset into our address space of the location to which we start
  // the DMA engine.
  uint32_t GetBank0Offset() const;
  uint32_t GetBank0OffsetUv() const;
  size_t GetImageSize() const;

  // Gets the value that should be written to the plane_select register when
  // configuring the format of the ISP.
  uint8_t GetPlaneSelect() const;
  uint8_t GetPlaneSelectUv() const;
  // Gets the value that should be written to the base_mode register when
  // configuring the format of the ISP.
  uint8_t GetBaseMode() const;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

 private:
  uint32_t width_, height_;
  bool flip_vertical_ = false;
  uint8_t base_mode_ = PixelType::INVALID;
  uint8_t secondary_plane_select_ = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_FORMAT_H_
