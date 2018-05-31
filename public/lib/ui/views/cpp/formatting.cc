// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/views/cpp/formatting.h"

#include <ostream>

namespace fuchsia {
namespace ui {
namespace views_v1 {

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewTreeToken& value) {
  return os << "<T" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewInfo& value) {
  return os << "{}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewProperties& value) {
  return os << "{display_metrics=" << *value.display_metrics
            << ", view_layout=" << *value.view_layout << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::DisplayMetrics& value) {
  return os << "{device_pixel_ratio=" << value.device_pixel_ratio << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewLayout& value) {
  return os << "{size=" << value.size << ", inset=" << value.inset << "}";
}

}  // namespace views_v1
}  // namespace ui
}  // namespace fuchsia

namespace fuchsia {
namespace ui {
namespace views_v1_token {

std::ostream& operator<<(
    std::ostream& os, const ::fuchsia::ui::views_v1_token::ViewToken& value) {
  return os << "<V" << value.value << ">";
}

}  // namespace views_v1_token
}  // namespace ui
}  // namespace fuchsia
