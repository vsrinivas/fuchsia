// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_GEOMETRY_CPP_FORMATTING_H_
#define LIB_UI_GEOMETRY_CPP_FORMATTING_H_

#include <fuchsia/cpp/geometry.h>

namespace mozart {

std::ostream& operator<<(std::ostream& os, const geometry::Point& value);
std::ostream& operator<<(std::ostream& os, const geometry::PointF& value);
std::ostream& operator<<(std::ostream& os, const geometry::Rect& value);
std::ostream& operator<<(std::ostream& os, const geometry::RectF& value);
std::ostream& operator<<(std::ostream& os, const geometry::RRectF& value);
std::ostream& operator<<(std::ostream& os, const geometry::Size& value);
std::ostream& operator<<(std::ostream& os, const geometry::SizeF& value);
std::ostream& operator<<(std::ostream& os, const geometry::Inset& value);
std::ostream& operator<<(std::ostream& os, const geometry::InsetF& value);
std::ostream& operator<<(std::ostream& os, const geometry::Transform& value);

}  // namespace mozart

#endif  // LIB_UI_GEOMETRY_CPP_FORMATTING_H_
