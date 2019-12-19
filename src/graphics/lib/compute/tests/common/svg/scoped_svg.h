// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SCOPED_SVG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SCOPED_SVG_H_

#include <memory>

#include "svg/svg.h"

// Convenience C++ class to hold a temporary svg object.
class ScopedSvg {
 public:
  ScopedSvg() = default;

  explicit ScopedSvg(svg * svg) : svg_(svg)
  {
  }

  // Create new instance by parsing an SVG file
  static ScopedSvg
  parseFile(const char * file_path)
  {
    return ScopedSvg(svg_open(file_path, false));
  }

  // Create new instance by parsing an SVG document
  static ScopedSvg
  parseText(const char * text)
  {
    return ScopedSvg(svg_parse(text, false));
  }

  // Access the underlying svg object. Note that this does not return
  // a const svg* because parsing the content of the document mutates
  // internal iterators in it :-(
  svg *
  get() const
  {
    return svg_.get();
  }

  // Return the number of paths in svg
  uint32_t
  path_count() const
  {
    return svg_path_count(get());
  }

  // Return the number of rasters in svg
  uint32_t
  raster_count() const
  {
    return svg_raster_count(get());
  }

  // Return the number of layers in svg
  uint32_t
  layer_count() const
  {
    return svg_layer_count(get());
  }

 protected:
  struct Deleter
  {
    void
    operator()(svg * ptr) noexcept
    {
      svg_dispose(ptr);
    }
  };

  std::unique_ptr<svg, Deleter> svg_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SCOPED_SVG_H_
