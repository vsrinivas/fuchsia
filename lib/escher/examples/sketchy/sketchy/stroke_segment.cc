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

  FXL_DCHECK(!std::isnan(curve_.pts[0].x));
  FXL_DCHECK(!std::isnan(curve_.pts[0].y));
  FXL_DCHECK(!std::isnan(curve_.pts[1].x));
  FXL_DCHECK(!std::isnan(curve_.pts[1].y));
  FXL_DCHECK(!std::isnan(curve_.pts[2].x));
  FXL_DCHECK(!std::isnan(curve_.pts[2].y));
  FXL_DCHECK(!std::isnan(curve_.pts[3].x));
  FXL_DCHECK(!std::isnan(curve_.pts[3].y));
  FXL_DCHECK(!std::isnan(arc_length_parameterization_.pts[0]));
  FXL_DCHECK(!std::isnan(arc_length_parameterization_.pts[1]));
  FXL_DCHECK(!std::isnan(arc_length_parameterization_.pts[2]));
  FXL_DCHECK(!std::isnan(arc_length_parameterization_.pts[3]));
  FXL_DCHECK(!std::isnan(length_));
}

}  // namespace sketchy
