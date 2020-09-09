// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_FACTORY_CAPTURE_H_
#define SRC_CAMERA_BIN_FACTORY_CAPTURE_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include <png.h>

namespace camera {

enum class WriteFlags : uint32_t {
  NONE =         0,

  // specify 1 input format
  IN_DEFAULT =      1 <<  0,  // use PixelFormatType in frame
  IN_NV12 =         1 <<  1,  // ouverride, assume 8-bit YUV in 2 planes
  IN_BAYER8 =       1 <<  2,  // override, assume 8-bit gray bayer
  IN_BAYER16 =      1 <<  3,  // override, assume 16-bit gray, RGRGRGRG... GBGBGBGB...

  // specify 1 output format
  OUT_RAW =         1 <<  8,  // 16-bit grayscale, possibly byte swapped, no header
  OUT_PGM =         1 <<  9,  // 16-bit grayscale pgm
  OUT_PNG_GRAY =    1 << 10,  // png, 16-bit gray
  OUT_PPM =         1 << 11,  // 8-bit RGB ppm
  OUT_PNG_RGB =     1 << 12,  // 8-bit RGB png

  // specify 0 or more modifications
  MOD_SWAP =        1 << 16,  // when writing 16-bit gray, swap bytes
  MOD_CENTER =      1 << 17,  // ignore crop x, y, align on center of image
  MOD_UNPROCESSED = 1 << 18,  // do not do any image
  MOD_BAYER8HACK =  1 << 19,  // tweak properties to match what ISP sends
};

constexpr inline WriteFlags operator|(WriteFlags _lhs, WriteFlags _rhs) {
  return static_cast<WriteFlags>(static_cast<uint32_t>(_lhs) | static_cast<uint32_t>(_rhs));
}

constexpr inline WriteFlags operator&(WriteFlags _lhs, WriteFlags _rhs) {
  return static_cast<WriteFlags>(static_cast<uint32_t>(_lhs) & static_cast<uint32_t>(_rhs));
}

const auto kInMask = (WriteFlags::IN_DEFAULT | WriteFlags::IN_NV12 | WriteFlags::IN_BAYER16 |
                      WriteFlags::IN_BAYER8);
const auto kOutMask = (WriteFlags::OUT_RAW | WriteFlags::OUT_PGM | WriteFlags::OUT_PNG_GRAY |
                       WriteFlags::OUT_PPM | WriteFlags::OUT_PNG_RGB);
const auto kModMask = (WriteFlags::MOD_SWAP | WriteFlags::MOD_CENTER | WriteFlags::MOD_UNPROCESSED);
const auto k16bitMask = (WriteFlags::OUT_RAW | WriteFlags::OUT_PGM | WriteFlags::OUT_PNG_GRAY);
const auto kGrayMask = k16bitMask;
const auto kPNGMask = (WriteFlags::OUT_PNG_GRAY | WriteFlags::OUT_PNG_RGB);
const auto kPNMMask = (WriteFlags::OUT_PGM | WriteFlags::OUT_PPM);

// sub-rect to crop to
// crop must use even coords (bayer and nv12 require that)
struct Crop {
 public:
  uint32_t x, y;
  uint32_t width, height;
};

// state for fetching rows of data from frame
struct ImageIter {
 public:
  uint32_t pos = 0;
  uint32_t stride = 0;
};

class Capture;
using CaptureResponse = fit::function<void(zx_status_t, std::unique_ptr<Capture>)>;
using ConversionMethod = void(Capture::*)(ImageIter[2], Crop&, std::vector<uint8_t>&);

class Capture {
 public:
  static fit::result<std::unique_ptr<Capture>, zx_status_t> Create(uint32_t stream,
                                                                   const std::string path,
                                                                   bool want_image,
                                                                   CaptureResponse callback);
  Capture();
  ~Capture() = default;

  // part of request
  uint32_t stream_;
  bool want_image_;
  CaptureResponse callback_;

  // part of response
  std::unique_ptr<std::basic_string<uint8_t>> image_;  // vmo bits if want_image_ is true
  fuchsia::camera3::StreamProperties properties_;

  // write frame data, converting, cropping and swapping if requested
  // crop is modified to return the actual dimensions written
  // crop is intersected with size of image (0 width/height means full image)
  // returns ZX_ERR_NOT_SUPPORTED if frame is an unsupported PixelFormat or flags
  // returns ZX_ERR_INVALID_ARGS if crop is not even coords
  zx_status_t WriteImage(FILE* fp, WriteFlags flags, Crop& crop);

  // write frame data assuming it's NV12, convert to RGB
  void WritePNGAsNV12(FILE* fp);

  // write frame data assuming as 8-bit gray.  YUV/NV12 will show 2 planes, Y then UV.
  // if is_bayer is true, just output the Y plane (top 2/3 of height)
  void WritePNGUnprocessed(FILE* fp, bool is_bayer);

  bool is_png_error_ = false;

 private:
  // verify legal imputs
  zx_status_t ValidateWriteFlags(WriteFlags flags);

  // intersect crop rect with image, optionally centering
  void IntersectCrop(Crop& crop, WriteFlags flags);

  // conversion methods
  void YOnly(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row_out);
  void RawBayer8(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row_out);
  void RawBayer16(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row_out);
  void YUVToRGB(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row_out);
  void Unprocessed(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row_out);
  void Demosaic(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row_out);

  // png output
  void PNGStart(uint32_t width, uint32_t height, uint32_t depth, WriteFlags flags, FILE* fp);
  void PNGFinish();

  png_structp png_ptr_ = nullptr;
  png_infop png_info_ = nullptr;

};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_FACTORY_CAPTURE_H_
