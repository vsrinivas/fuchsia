// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/views/cpp/formatting.h"

#include <ostream>

namespace fuchsia {
namespace ui {
namespace viewsv1 {

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::viewsv1::ViewTreeToken& value) {
  return os << "<T" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::viewsv1::ViewInfo& value) {
  return os << "{}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::viewsv1::ViewProperties& value) {
  return os << "{view_layout=" << *value.view_layout << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::viewsv1::ViewLayout& value) {
  return os << "{size=" << value.size << ", inset=" << value.inset << "}";
}

}  // namespace viewsv1
}  // namespace ui
}  // namespace fuchsia

namespace fuchsia {
namespace ui {
namespace viewsv1token {

std::ostream& operator<<(
    std::ostream& os, const ::fuchsia::ui::viewsv1token::ViewToken& value) {
  return os << "<V" << value.value << ">";
}

}  // namespace viewsv1token
}  // namespace ui
}  // namespace fuchsia
