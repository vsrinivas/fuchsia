// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_bounds.h"

#include "tests/common/affine_transform.h"
#include "tests/common/path_sink.h"
#include "tests/common/svg/svg_utils.h"

void
svg_estimate_bounds(const struct svg * const   sd,
                    const affine_transform_t * transform,
                    double * const             xmin,
                    double * const             ymin,
                    double * const             xmax,
                    double * const             ymax)
{
  BoundingPathSink sink;

  svg_decode_rasters(sd, transform, [&sink](const SvgDecodedRaster & raster) -> bool {
    return svg_decode_path(raster.svg, raster.path_id, &raster.transform, &sink);
  });

  *xmin = sink.bounds().xmin;
  *ymin = sink.bounds().ymin;
  *xmax = sink.bounds().xmax;
  *ymax = sink.bounds().ymax;
}
