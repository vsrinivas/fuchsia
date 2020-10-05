// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/capture.h"

#include <lib/syslog/cpp/macros.h>

#include <functional>
#include <memory>

namespace camera {

fit::result<std::unique_ptr<Capture>, zx_status_t> Capture::Create(uint32_t stream,
                                                                   const std::string path,
                                                                   bool want_image,
                                                                   CaptureResponse callback) {
  auto capture = std::make_unique<Capture>();
  capture->stream_ = stream;
  capture->want_image_ = want_image;
  capture->image_ = std::make_unique<std::basic_string<uint8_t>>();
  capture->callback_ = std::move(callback);
  return fit::ok(std::move(capture));
}

Capture::Capture() {}

zx_status_t Capture::ValidateWriteFlags(WriteFlags flags) {
  WriteFlags in_fmt = flags & kInMask;
  switch (in_fmt) {
    case WriteFlags::IN_NV12:
    case WriteFlags::IN_BAYER8:
    case WriteFlags::IN_BAYER16:
    case WriteFlags::IN_DEFAULT:
      // OK
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  WriteFlags out_fmt = flags & kOutMask;
  switch (out_fmt) {
    case WriteFlags::OUT_RAW:
    case WriteFlags::OUT_PGM:
    case WriteFlags::OUT_PNG_GRAY:
    case WriteFlags::OUT_PPM:
    case WriteFlags::OUT_PNG_RGB:
      // OK
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // unprocessed has to be gray
  if ((flags & WriteFlags::MOD_UNPROCESSED) != WriteFlags::NONE &&
      (flags & kGrayMask) == WriteFlags::NONE) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void Capture::IntersectCrop(Crop& crop, WriteFlags flags) {
  auto& iformat = properties_.image_format;
  auto width = iformat.coded_width;
  auto height = iformat.coded_height;

  bool center_flag = (flags & WriteFlags::MOD_CENTER) == WriteFlags::MOD_CENTER;

  // 0 with or height means actual w/h
  if (crop.width == 0) {
    crop.width = width;
  }
  if (crop.height == 0) {
    crop.height = height;
  }
  // clear x,y if centering
  if (center_flag) {
    crop.x = 0;
    crop.y = 0;
  }

  struct Rect {
    uint32_t x0, y0, x1, y1;
  };

  Rect max = {0, 0, width - 1, height - 1};
  Rect want = {crop.x, crop.y, crop.x + crop.width, crop.y + crop.height};

  if (want.x0 > max.x1)
    want.x0 = max.x1;
  if (want.y0 > max.y1)
    want.y0 = max.y1;
  if (want.x1 > max.x1)
    want.x1 = max.x1;
  if (want.y1 > max.y1)
    want.y1 = max.y1;

  // & ~1 means round down to even; +1 & ~1 means round up to even; even is for YUV and bayer

  crop.width = (want.x1 - want.x0 + 1) & ~1;
  crop.height = (want.y1 - want.y0 + 1) & ~1;
  if (crop.width < 2)
    crop.width = 2;
  if (crop.height < 2)
    crop.height = 2;

  if (center_flag) {
    crop.x = (width / 2 - crop.width / 2) & ~1;
    crop.y = (height / 2 - crop.height / 2) & ~1;
  } else {
    crop.x = want.x0 & ~1;
    crop.y = want.y0 & ~1;
  }
}

zx_status_t Capture::WriteImage(FILE* fp, WriteFlags flags, Crop& crop) {
  zx_status_t status;

  status = ValidateWriteFlags(flags);
  if (status != ZX_OK) {
    return status;
  }

  auto& iformat = properties_.image_format;
  // workaround for bypass mode wrapped in NV16
  auto is_bayer_hack = (flags & WriteFlags::MOD_BAYER8HACK) != WriteFlags::NONE;
  if (is_bayer_hack && iformat.pixel_format.type == fuchsia::sysmem::PixelFormatType::NV12) {
    // TODO(fxbug.dev/58283) remove when stream format is accurate for bayer mode
    // 2200x2720 is from sensor config
    iformat.bytes_per_row = 2208;  // rounded up to %16?
    iformat.coded_width = 2200;
    iformat.coded_height = 2680;  // -40 extra lines, determined by visual inspection
  }

  if ((flags & WriteFlags::MOD_UNPROCESSED) != WriteFlags::NONE) {
    // unprocessed means return whole frame, even it it's non-image data
    iformat.coded_width = iformat.bytes_per_row;
    iformat.coded_height = image_->size() / iformat.bytes_per_row;
  }

  IntersectCrop(crop, flags);

  WriteFlags in_fmt = flags & kInMask;
  WriteFlags out_fmt = flags & kOutMask;

  uint32_t in_pixel_size = 1;
  if (in_fmt == WriteFlags::IN_DEFAULT) {
    switch (iformat.pixel_format.type) {
      case fuchsia::sysmem::PixelFormatType::NV12:
        in_fmt = is_bayer_hack ? WriteFlags::IN_BAYER8 : WriteFlags::IN_NV12;
        break;
      case fuchsia::sysmem::PixelFormatType::RGB565:
        // fxbug.dev(58283): should be VK_FORMAT_R16_UNORM compatible
        in_fmt = WriteFlags::IN_BAYER16;
        break;
      default:
        return ZX_ERR_NOT_SUPPORTED;
    }
  }
  if (in_fmt == WriteFlags::IN_BAYER16) {
    in_pixel_size = 2;
  }

  auto swap_requested = (flags & WriteFlags::MOD_SWAP) == WriteFlags::MOD_SWAP;

  // swap is only for RAW mode
  if (swap_requested && (out_fmt != WriteFlags::OUT_RAW || in_pixel_size != 2)) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto is_16_bit = (flags & k16bitMask) != WriteFlags::NONE;
  uint32_t out_pixel_size = is_16_bit ? 2 : 3;  // 16-bit gray or 8-bit RGB
  uint32_t out_depth = is_16_bit ? 16 : 8;
  bool is_png = (flags & kPNGMask) != WriteFlags::NONE;
  bool is_pnm = (flags & kPNMMask) != WriteFlags::NONE;

  ConversionMethod method = nullptr;
  auto gray_requested = (flags & kGrayMask) != WriteFlags::NONE;
  if ((flags & WriteFlags::MOD_UNPROCESSED) != WriteFlags::NONE) {
    method = &Capture::Unprocessed;
    out_depth = 8;
    out_pixel_size = 1;
  } else if (in_fmt == WriteFlags::IN_NV12) {
    method = gray_requested ? &Capture::YOnly : &Capture::YUVToRGB;
  } else if (in_fmt == WriteFlags::IN_BAYER8 && gray_requested) {
    method = &Capture::RawBayer8;
  } else if (in_fmt == WriteFlags::IN_BAYER16 && gray_requested) {
    method = &Capture::RawBayer16;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (is_png) {
    PNGStart(crop.width, crop.height, out_depth, flags, fp);
  } else if (is_pnm) {
    // see pgm(5) & ppm(5)
    fprintf(fp, "P%d %d %d %d\n", out_fmt == WriteFlags::OUT_PGM ? 5 : 6, crop.width, crop.height,
            (1 << out_depth) - 1);
  }
  // else: raw has no header

  auto stride = iformat.bytes_per_row;
  ImageIter plane[2] = {
      // for YUV, [0] is Y, [1] is UV; [1] used for NV12 only
      {crop.x * in_pixel_size + crop.y * stride, stride},
      {iformat.coded_height * stride + crop.x * in_pixel_size + crop.y / 2, stride}};

  std::vector<uint8_t> row;
  row.resize(crop.width * out_pixel_size);

  for (uint32_t i = 0; i < crop.height; i++) {
    std::invoke(method, this, plane, crop, row);

    if (swap_requested) {
      for (uint32_t i = 0; i < crop.width; i++) {
        auto tmp = row[i * out_pixel_size];
        row[i * out_pixel_size] = row[i * out_pixel_size + 1];
        row[i * out_pixel_size + 1] = tmp;
      }
    }

    if (is_png) {
      png_write_row(png_ptr_, row.data());
    } else {
      fwrite(row.data(), 1, row.size(), fp);
    }

    plane[0].pos += plane[0].stride;
    if (i % 2 == 1) {  // 1 UV row for every 2 Y rows
      plane[1].pos += plane[1].stride;
    }
  }
  if (is_png) {
    PNGFinish();
  }
  fflush(fp);
  return is_png_error_ ? ZX_ERR_INTERNAL : ZX_OK;
}

// 8-bit Y to 16-bit gray
void Capture::YOnly(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row) {
  auto in = image_->data() + plane[0].pos;
  auto out = row.data();
  for (uint32_t i = 0; i < crop.width; i++) {
    *out++ = *in;
    *out++ = *in++;
  }
}

// 8-bit sensor data to 16-bit gray
void Capture::RawBayer8(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row) {
  auto in = image_->data() + plane[0].pos;
  auto out = row.data();
  for (uint32_t i = 0; i < crop.width; i++) {
    // fxbug.dev(58283) should copy 10 bit bayer to 16-bit gray
    *out++ = *in;
    *out++ = *in++;
  }
}

// 10+-bit sensor data to 16-bit gray
void Capture::RawBayer16(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row) {
  auto in = image_->data() + plane[0].pos;
  auto out = row.data();
  for (uint32_t i = 0; i < crop.width; i++) {
    // fxbug.dev(58283) Do we need to shift?
    *out++ = *in++;
    *out++ = *in++;
  }
}

void Capture::YUVToRGB(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row) {
  auto ypos = image_->data() + plane[0].pos;
  auto uvpos = image_->data() + plane[1].pos;
  for (uint32_t j = 0; j < crop.width; j++) {
    int32_t y = ypos[j];
    int32_t u = uvpos[(j / 2) * 2];
    int32_t v = uvpos[(j / 2) * 2 + 1];
    // android algorithm
    int rTmp = y + (1.370705 * (v - 128));
    int gTmp = y - (0.698001 * (v - 128)) - (0.337633 * (u - 128));
    int bTmp = y + (1.732446 * (u - 128));
#define CLIP(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))
    uint32_t r = CLIP(rTmp);
    uint32_t g = CLIP(gTmp);
    uint32_t b = CLIP(bTmp);
    row[j * 3 + 0] = r;
    row[j * 3 + 1] = g;
    row[j * 3 + 2] = b;
  }
}

// just copy rows
void Capture::Unprocessed(ImageIter plane[2], Crop& crop, std::vector<uint8_t>& row) {
  auto in = image_->data() + plane[0].pos;
  auto out = row.data();
  memcpy(out, in, crop.width);
}

static void png_error_fn(png_structp png_ptr, png_const_charp msg) {
  auto capture = static_cast<Capture*>(png_get_error_ptr(png_ptr));
  capture->is_png_error_ = true;
  FX_LOGS(ERROR) << "png error: " << msg;
}

static void png_warning_fn(png_structp png_ptr, png_const_charp msg) {
  FX_LOGS(WARNING) << "png warning: " << msg;
}

void Capture::PNGStart(uint32_t width, uint32_t height, uint32_t depth, WriteFlags flags,
                       FILE* fp) {
  is_png_error_ = false;
  png_ptr_ = png_create_write_struct(PNG_LIBPNG_VER_STRING, this, png_error_fn, png_warning_fn);
  png_info_ = png_create_info_struct(png_ptr_);

  png_init_io(png_ptr_, fp);
  auto is_gray = (flags & WriteFlags::OUT_PNG_GRAY) == WriteFlags::OUT_PNG_GRAY;
  auto type = is_gray ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB;
  png_set_IHDR(png_ptr_, png_info_, width, height, depth, type, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr_, png_info_);
}

void Capture::PNGFinish() {
  if (!is_png_error_) {
    png_write_end(png_ptr_, NULL);
  }
  png_destroy_write_struct(&png_ptr_, &png_info_);
}

void Capture::WritePNGAsNV12(FILE* fp) {
  Crop crop = {0, 0, 0, 0};
  WriteImage(fp, WriteFlags::IN_NV12 | WriteFlags::OUT_PNG_RGB, crop);
}

void Capture::WritePNGUnprocessed(FILE* fp, bool is_bayer) {
  Crop crop = {0, 0, 0, 0};
  WriteImage(fp,
             WriteFlags::IN_DEFAULT | WriteFlags::OUT_PNG_GRAY | WriteFlags::MOD_UNPROCESSED |
                 (is_bayer ? WriteFlags::MOD_BAYER8HACK : WriteFlags::NONE),
             crop);
}

}  // namespace camera
