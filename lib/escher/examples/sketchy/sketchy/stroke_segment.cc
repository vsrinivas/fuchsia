// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sketchy/stroke_segment.h"

#include "escher/shape/mesh_builder.h"
#include "sketchy/debug_print.h"
#include "sketchy/page.h"

namespace sketchy {

StrokeSegment::StrokeSegment(CubicBezier2f curve) : curve_(curve) {
  auto pair = curve_.ArcLengthParameterization();
  arc_length_parameterization_ = pair.first;
  length_ = pair.second;

  FTL_DCHECK(!std::isnan(curve_.pts[0].x));
  FTL_DCHECK(!std::isnan(curve_.pts[0].y));
  FTL_DCHECK(!std::isnan(curve_.pts[1].x));
  FTL_DCHECK(!std::isnan(curve_.pts[1].y));
  FTL_DCHECK(!std::isnan(curve_.pts[2].x));
  FTL_DCHECK(!std::isnan(curve_.pts[2].y));
  FTL_DCHECK(!std::isnan(curve_.pts[3].x));
  FTL_DCHECK(!std::isnan(curve_.pts[3].y));
  FTL_DCHECK(!std::isnan(arc_length_parameterization_.pts[0]));
  FTL_DCHECK(!std::isnan(arc_length_parameterization_.pts[1]));
  FTL_DCHECK(!std::isnan(arc_length_parameterization_.pts[2]));
  FTL_DCHECK(!std::isnan(arc_length_parameterization_.pts[3]));
  FTL_DCHECK(!std::isnan(length_));
}

}  // namespace sketchy
