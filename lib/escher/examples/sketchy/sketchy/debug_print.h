// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>

#include "sketchy/types.h"

#if !defined(NDEBUG)

#include "sketchy/cubic_bezier.h"
#include "sketchy/stroke.h"

inline std::ostream& operator<<(std::ostream& os, const sketchy::vec2& pt) {
  return os << "(" << pt.x << "," << pt.y << ")";
}

inline std::ostream& operator<<(std::ostream& os, const sketchy::vec3& pt) {
  return os << "(" << pt.x << "," << pt.y << "," << pt.z << ")";
}

template <typename VecT>
std::ostream& operator<<(std::ostream& os,
                         const sketchy::CubicBezier<VecT>& bez) {
  return os << "p0=" << bez.pts[0] << ", "
            << "p1=" << bez.pts[1] << ", "
            << "p2=" << bez.pts[2] << ", "
            << "p3=" << bez.pts[3];
}

inline std::ostream& operator<<(std::ostream& os,
                                const sketchy::StrokeSegment& seg) {
  return os << seg.curve() << "  len=" << seg.length();
}

inline std::ostream& operator<<(std::ostream& os,
                                const sketchy::Stroke& stroke) {
  auto& path = stroke.path();
  os << "STROKE (id: " << stroke.id() << "  #segs: " << path.size() << ")";
  for (size_t i = 0; i < path.size(); ++i) {
    os << std::endl << "      seg " << i << ":  " << path[i];
  }
  return os;
}

#else

namespace sketchy {

template <typename VecT>
struct CubicBezier;

class Stroke;

}  // namespace sketchy

inline std::ostream& operator<<(std::ostream& os, const sketchy::vec2& pt) {
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const sketchy::vec3& pt) {
  return os;
}

template <typename VecT>
std::ostream& operator<<(std::ostream& os,
                         const sketchy::CubicBezier<VecT>& bez) {
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const sketchy::StrokeSegment& seg) {
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const sketchy::Stroke& stroke) {
  return os;
}

#endif
