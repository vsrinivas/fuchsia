
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/image-format-llcpp/image-format-llcpp.h>

#include <type_traits>

namespace sysmem = fuchsia_sysmem;

namespace image_format {

fuchsia_sysmem_ImageFormatConstraints GetCConstraints(
    const fuchsia_sysmem::wire::ImageFormatConstraints& cpp) {
  fuchsia_sysmem_ImageFormatConstraints c;
  static_assert(std::is_trivially_copyable<fuchsia_sysmem::wire::ImageFormatConstraints>::value,
                "Not trivially copyable");
  static_assert(sizeof(c) == sizeof(cpp), "LLCPP and C image format constraints don't match");
  // Hacky copy that should work for now.
  // TODO(fxbug.dev/37078): Switch away from C bindings everywhere.
  memcpy(&c, &cpp, sizeof(c));
  return c;
}

fuchsia_sysmem::wire::PixelFormat GetCppPixelFormat(const fuchsia_sysmem_PixelFormat& c) {
  sysmem::wire::PixelFormat cpp;
  static_assert(sizeof(cpp) == sizeof(c), "LLCPP and C pixel formats don't match");
  static_assert(std::is_trivially_copyable<fuchsia_sysmem::wire::PixelFormat>::value,
                "Not trivially copyable");
  // Hacky copy that should work for now. We need the static_cast because sysmem::wire::PixelFormat
  // is a non-trivial class and otherwise GCC complains.
  // TODO(fxbug.dev/37078): Switch away from C bindings everywhere.
  memcpy(static_cast<void*>(&cpp), &c, sizeof(c));
  return cpp;
}

fuchsia_sysmem_PixelFormat GetCPixelFormat(const fuchsia_sysmem::wire::PixelFormat& cpp) {
  fuchsia_sysmem_PixelFormat c;
  static_assert(std::is_trivially_copyable<fuchsia_sysmem::wire::PixelFormat>::value,
                "Not trivially copyable");
  static_assert(sizeof(c) == sizeof(cpp), "LLCPP and C pixel format don't match");
  // Hacky copy that should work for now.
  // TODO(fxbug.dev/37078): Switch away from C bindings everywhere.
  memcpy(&c, &cpp, sizeof(c));
  return c;
}

fuchsia_sysmem::wire::ImageFormat2 GetCppImageFormat(const fuchsia_sysmem_ImageFormat_2& c) {
  sysmem::wire::ImageFormat2 cpp;
  static_assert(sizeof(cpp) == sizeof(c), "LLCPP and C image formats don't match");
  static_assert(std::is_trivially_copyable<fuchsia_sysmem::wire::ImageFormat2>::value,
                "Not trivially copyable");
  // Hacky copy that should work for now. We need the static_cast because sysmem::wire::PixelFormat
  // is a non-trivial class and otherwise GCC complains.
  // TODO(fxbug.dev/37078): Switch away from C bindings everywhere.
  memcpy(static_cast<void*>(&cpp), &c, sizeof(c));
  return cpp;
}

fuchsia_sysmem_ImageFormat_2 GetCImageFormat(const fuchsia_sysmem::wire::ImageFormat2& cpp) {
  fuchsia_sysmem_ImageFormat_2 c;
  static_assert(std::is_trivially_copyable<fuchsia_sysmem::wire::ImageFormat2>::value,
                "Not trivially copyable");
  static_assert(sizeof(c) == sizeof(cpp), "LLCPP and C image formats don't match");
  // Hacky copy that should work for now.
  // TODO(fxbug.dev/37078): Switch away from C bindings everywhere.
  memcpy(&c, &cpp, sizeof(c));
  return c;
}

bool GetMinimumRowBytes(const fuchsia_sysmem::wire::ImageFormatConstraints& constraints,
                        uint32_t width, uint32_t* bytes_per_row_out) {
  fuchsia_sysmem_ImageFormatConstraints c_constraints = GetCConstraints(constraints);
  return ImageFormatMinimumRowBytes(&c_constraints, width, bytes_per_row_out);
}

std::optional<fuchsia_sysmem::wire::ImageFormat2> ConstraintsToFormat(
    const fuchsia_sysmem::wire::ImageFormatConstraints& constraints, uint32_t coded_width,
    uint32_t coded_height) {
  auto c_constraints = GetCConstraints(constraints);
  fuchsia_sysmem_ImageFormat_2 image_format;
  if (!ImageConstraintsToFormat(&c_constraints, coded_width, coded_height, &image_format))
    return {};
  return {GetCppImageFormat(image_format)};
}

bool GetPlaneByteOffset(const fuchsia_sysmem::wire::ImageFormat2& image_format, uint32_t plane,
                        uint64_t* offset_out) {
  fuchsia_sysmem_ImageFormat_2 c_constraints = GetCImageFormat(image_format);
  return ImageFormatPlaneByteOffset(&c_constraints, plane, offset_out);
}

bool GetPlaneRowBytes(const fuchsia_sysmem::wire::ImageFormat2& image_format, uint32_t plane,
                      uint32_t* row_bytes_out) {
  fuchsia_sysmem_ImageFormat_2 c_constraints = GetCImageFormat(image_format);
  return ImageFormatPlaneRowBytes(&c_constraints, plane, row_bytes_out);
}

bool FormatCompatibleWithProtectedMemory(const fuchsia_sysmem::wire::PixelFormat& format) {
  fuchsia_sysmem_PixelFormat c_pixel_format = GetCPixelFormat(format);
  return ImageFormatCompatibleWithProtectedMemory(&c_pixel_format);
}

}  // namespace image_format
