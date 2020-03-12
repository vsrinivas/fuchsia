// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_UTILS_H_

#include <functional>
#include <vector>

#include "svg/svg.h"
#include "tests/common/affine_transform.h"
#include "tests/common/path_sink.h"

// Helper functions to parse the content of a given svg document.

// Parse a specific path in an |svg| document, identified by its |path_id|,
// and send the resulting path items to |target|. Return true on success, or
// false on failure.
extern bool
svg_decode_path(const struct svg *         svg,
                uint32_t                   path_id,
                const affine_transform_t * transform,
                PathSink *                 target);

// A struct describing a decoded raster entry from an svg instance.
// |svg| is the input svg document.
// |raster_id| is the raster's index in the document.
// |path_id| is the matching path index in the document.
// |transform| is the affine transform applied to the path to create the raster.
struct SvgDecodedRaster
{
  const svg *        svg;
  uint32_t           raster_id;
  uint32_t           path_id;
  affine_transform_t transform;

  // A callable type that takes a const reference to an SvgDecodedRaster
  // and returns true on success, or false on failure.
  // Used in svg_decode_rasters() below. If this returns false, decoding will
  // stop and force the latter function to return false as well.
  using Callback = std::function<bool(const SvgDecodedRaster &)>;
};

// Parse all rasters in an input document, and invoke |callback| for each one
// of them in succession. |transform| is an optional initial transform to be
// applied to all rasters. Return true on success, or false on failure
// (which is defined as one of the callback invocation returning false).
//
// NOTE: Due to the way transforms are encoded in a struct svg instance,
// decoding individual rasters is not efficient, so no API is provided to do
// that.
extern bool
svg_decode_rasters(const struct svg *         svg,
                   const affine_transform_t * transform,
                   SvgDecodedRaster::Callback callback);

// A struct describing a decoded layer entry from an svg instance.
// |svg| is the input svg document.
// |layer_id| is the layer's index in the document.
// |fill_color| is the fill color to be applied to all rasters in the layer.
// |fill_opacity| is the fill opacity to be applied to all rasters in the
// layer.
// |opacity| is another opacity parameter to be applied to |fill_opacity|.
// |fill_even_odd| will be true if rasters must be filled using the even-odd
// rule in this layer. Otherwise, the default non-zero winding rule will be
// used instead.
// |prints| is an array of placed raster references in the layer.
struct SvgDecodedLayer
{
  const svg * svg           = nullptr;
  uint32_t    layer_id      = 0;
  svg_color_t fill_color    = 0;
  float       fill_opacity  = 1.;
  float       opacity       = 1.;
  bool        fill_even_odd = false;

  struct Print
  {
    uint32_t raster_id;
    int32_t  tx, ty;
  };

  std::vector<Print> prints;

  // A callable type that takes a const reference to a SvgDecodedLayer and
  // returns true on success, or false on failure.
  // Used by svg_decode_layers() below. If this returns false, decoding
  // will stop and force the latter function will return false.
  using Callback = std::function<bool(const SvgDecodedLayer &)>;
};

// Parse all layers in an input document, and invoke |callback| for each one
// of them in succession. Return true on success, or false on failure, (which
// is defined as one of the callback invocation returning false).
//
// Note that layer id are decoded in increasing order, as they appear in the
// input document. When rendering with Spinel, one may prefer to reverse the
// order, i.e. by using (svg_layer_count(l.svg) - 1 - l.layer_id) as the ID
// encoded in an spn_styling_t.
//
// NOTE: Due to the way layers are encoded in a struct svg instance,
// decoding individual layers is not efficient, so not API is provided
// to do that.
extern bool
svg_decode_layers(const struct svg * svg, SvgDecodedLayer::Callback callback);

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_UTILS_H_
