
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/image-format-llcpp/image-format-llcpp.h>

#include <type_traits>

namespace sysmem = ::llcpp::fuchsia::sysmem;

namespace image_format {

fuchsia_sysmem_ImageFormatConstraints GetCConstraints(
    const llcpp::fuchsia::sysmem::ImageFormatConstraints& cpp) {
  fuchsia_sysmem_ImageFormatConstraints c;
  static_assert(std::is_trivially_copyable<llcpp::fuchsia::sysmem::ImageFormatConstraints>::value,
                "Not trivially copyable");
  static_assert(sizeof(c) == sizeof(cpp), "LLCPP and C image format constraints don't match");
  // Hacky copy that should work for now.
  // TODO(fxb/37078): Switch away from C bindings everywhere.
  memcpy(&c, &cpp, sizeof(c));
  return c;
}

llcpp::fuchsia::sysmem::PixelFormat GetCppPixelFormat(const fuchsia_sysmem_PixelFormat& c) {
  sysmem::PixelFormat cpp;
  static_assert(sizeof(cpp) == sizeof(c), "LLCPP and C pixel formats don't match");
  static_assert(std::is_trivially_copyable<llcpp::fuchsia::sysmem::PixelFormat>::value,
                "Not trivially copyable");
  // Hacky copy that should work for now. We need the static_cast because sysmem::PixelFormat is a
  // non-trivial class and otherwise GCC complains.
  // TODO(fxb/37078): Switch away from C bindings everywhere.
  memcpy(static_cast<void*>(&cpp), &c, sizeof(c));
  return cpp;
}

bool GetMinimumRowBytes(const llcpp::fuchsia::sysmem::ImageFormatConstraints& constraints,
                        uint32_t width, uint32_t* bytes_per_row_out) {
  fuchsia_sysmem_ImageFormatConstraints c_constraints = GetCConstraints(constraints);
  return ImageFormatMinimumRowBytes(&c_constraints, width, bytes_per_row_out);
}

}  // namespace image_format
