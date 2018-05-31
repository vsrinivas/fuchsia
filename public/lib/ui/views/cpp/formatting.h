// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_VIEWS_CPP_FORMATTING_H_
#define LIB_UI_VIEWS_CPP_FORMATTING_H_

#include <iosfwd>

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/ui/geometry/cpp/formatting.h"

namespace fuchsia {
namespace ui {
namespace views_v1 {

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewTreeToken& value);

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewInfo& value);

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewProperties& value);
std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::DisplayMetrics& value);
std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1::ViewLayout& value);

}  // namespace views_v1
}  // namespace ui
}  // namespace fuchsia

namespace fuchsia {
namespace ui {
namespace views_v1_token {

std::ostream& operator<<(std::ostream& os,
                         const ::fuchsia::ui::views_v1_token::ViewToken& value);

}  // namespace views_v1_token
}  // namespace ui
}  // namespace fuchsia

#endif  // LIB_UI_VIEWS_CPP_FORMATTING_H_
