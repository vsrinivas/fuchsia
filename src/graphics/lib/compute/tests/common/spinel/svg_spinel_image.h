// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SVG_SPINEL_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SVG_SPINEL_IMAGE_H_

#include "spinel_image.h"

struct svg;

// A class to model a SpinelImage created by parsing an SVG document
// with the 'svg/svg.h' helper library.
//
struct SvgSpinelImage : public SpinelImage
{
 public:
  void
  init()
  {
  }
  void
  init(svg * svg, spn_context_t context, const SpinelImage::Config & config);
  void
  init(svg * svg, spn_context_t context);
  void
  reset();

  // Setup and reset path handles.
  void
  setupPaths();
  void
  resetPaths();

  // Setup and reset raster handles.

  // Setup the rasters. |transform| is an optional pointer to a transform that
  // will be applied to all paths in the input SVG document.
  void
  setupRasters(const spn_transform_t * transform);
  void
  resetRasters();

  // Setup and reset composition and styling.
  void
  setupLayers();
  void
  resetLayers();

  // Readonly accessors.
  const spn_path_t *
  paths() const
  {
    return paths_;
  }
  const spn_raster_t *
  rasters() const
  {
    return rasters_;
  }

 protected:
  svg *          svg_     = nullptr;
  spn_path_t *   paths_   = nullptr;
  spn_raster_t * rasters_ = nullptr;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SVG_SPINEL_IMAGE_H_
