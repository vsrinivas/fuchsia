// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CALIBRATION_IMAGE_WRITER_IMAGE_WRITER_H_
#define SRC_CAMERA_CALIBRATION_IMAGE_WRITER_IMAGE_WRITER_H_

#include <lib/fzl/vmo-pool.h>

#include <src/camera/drivers/isp/modules/dma-format.h>

namespace camera {

inline constexpr uint32_t kBytesPerDoublePixel = 3;
inline constexpr uint16_t kMaxVal = 4095;
inline constexpr uint16_t kRedPixel = 2048;
inline constexpr uint16_t kHighByteMask = 0xFF0;
inline constexpr uint16_t kLowHalfByteMask = 0x00F;
inline constexpr uint8_t kDoubleByteShift = 16;
inline constexpr uint8_t kByteShift = 8;
inline constexpr uint8_t kHalfByteShift = 4;
inline constexpr camera::DmaFormat::PixelType kPixelTypeRaw12 = camera::DmaFormat::RAW12;

class ImageWriter {
 public:
  // Constructor must be public to be instantiated with a unique pointer
  // Args:
  //  |dma_format| A format object consisting of width, height, pixel format, and other paramaters.
  //  |vmo_size| Number of bytes comprising the intended images to be written (width * height * # of
  //             bytes per pixel).
  ImageWriter(DmaFormat dma_format, size_t vmo_size)
      : dma_format_(dma_format), vmo_size_(vmo_size) {}

  // Factory method that outputs an ImageWriter with all its params set for the corresponding
  // format.
  // Args:
  //  |width| Intended width for images to be written (in # of pixels).
  //  |height| Intended height for images to be written (in # of pixels).
  //  |pixel_format| Intended byte representation for each pixel in the images to be written.
  // Returns:
  //  An object ImageWriter that writes images to vmos.
  static std::unique_ptr<ImageWriter> Init(uint32_t width, uint32_t height,
                                           camera::DmaFormat::PixelType pixel_format);

  // Creates a vmo of appropriate size (depending on DmaFormat image size) and passes it to be
  // filled by specified format handler.
  // Args:
  //  |vmo| Pointer to memory where the image will be written to.
  // Returns:
  //  Whether vmo creation succeeded.
  zx_status_t Write(zx::vmo* vmo);

  // Helper method that organizes two individual pixel values into the appropriate RAW12
  // double-pixel format.
  // Args:
  //  |first_pixel| A 12-bit value representing either a R or G pixel.
  //  |second_pixel| A 12-bit value representing either a G or B pixel.
  // Returns:
  //  An array of the bytes that were passed in arranged in RAW12 pixel format.
  static std::array<uint8_t, kBytesPerDoublePixel> PixelValuesToDoublePixel(
      uint16_t first_pixel_val, uint16_t second_pixel_val);
  // Helper method that extracts the original pixel values from a RAW12 double-pixel.
  // Args:
  //  |double_pixel| An array of bytes arranged in RAW12 pixel format (represents two pixels).
  // Returns:
  //  A tuple of each of the two 12-bit pixel values that comprised the original double-pixel.
  static std::tuple<uint16_t, uint16_t> DoublePixelToPixelValues(
      std::array<uint8_t, kBytesPerDoublePixel> double_pixel);

  // Getter methods
  camera::DmaFormat DmaFormat() const { return dma_format_; }
  size_t VmoSize() const { return vmo_size_; }

 private:
  // Fills a provided vmo with default data in the RAW12 image format. Only a RGGB layout is used at
  // the moment.
  // The red pixel value stays consistent at a value of 2048 throughout the image.
  // The blue pixel value increases from 0 to a maximum of 4095 across individual columns.
  // The green pixels value increases from 0 to a maximum of 4095 across individual rows.
  //
  // Each double-pixel is laid out like so:
  //  Bits  ->  7        6        5        4        3        2        1        0
  //  Byte0 ->  P0[11]   P0[10]   P0[9]    P0[8]    P0[7]    P0[6]    P0[5]    P0[4]
  //  Byte1 ->  P1[11]   P1[10]   P1[9]    P1[8]    P1[7]    P1[6]    P1[5]    P1[4]
  //  Byte2 ->  P1[3]    P1[2]    P1[1]    P1[0]    P0[3]    P0[2]    P0[1]    P0[0]
  //
  // A RG row with two double-pixels (six bytes) looks like:
  //  R0[11:4], G0[11:4], G0[3:0]R0[3:0], R1[11:4], G1[11:4], G1[3:0]R1[3:0]
  // A GB row with two double pixels (six bytes) looks like:
  //  G0[11:4], B0[11:4], B0[3:0]G0[3:0], G1[11:4], B1[11:4], B1[3:0]G1[3:0]
  // Args:
  //  |vmo| Pointer to memory where the image will be written to.
  void FillRAW12(zx::vmo* vmo);

  const camera::DmaFormat dma_format_;
  const size_t vmo_size_;
};

}  // namespace camera

#endif  // SRC_CAMERA_CALIBRATION_IMAGE_WRITER_IMAGE_WRITER_H_
