// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_VIEWS_CPP_VIEWS_UTIL_H_
#define LIB_UI_VIEWS_CPP_VIEWS_UTIL_H_

#include <fuchsia/cpp/views_v1.h>
#include "lib/ui/geometry/cpp/geometry_util.h"

namespace views_v1 {

inline bool operator==(const views_v1::ViewProperties& lhs,
                       const views_v1::ViewProperties& rhs) {
  return lhs.display_metrics == rhs.display_metrics &&
         lhs.view_layout == rhs.view_layout;
}

inline bool operator!=(const views_v1::ViewProperties& lhs,
                       const views_v1::ViewProperties& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const views_v1::DisplayMetricsPtr& lhs,
                       const views_v1::DisplayMetricsPtr& rhs) {
  if (lhs.get() == rhs.get()) {
    return true;
  }
  if (!lhs || !rhs) {
    return !lhs && !rhs;
  }
  return lhs->device_pixel_ratio == rhs->device_pixel_ratio;
}

inline bool operator!=(const views_v1::DisplayMetricsPtr& lhs,
                       const views_v1::DisplayMetricsPtr& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const views_v1::ViewLayoutPtr& lhs,
                       const views_v1::ViewLayoutPtr& rhs) {
  if (lhs.get() == rhs.get()) {
    return true;
  }
  if (!lhs || !rhs) {
    return !lhs && !rhs;
  }
  return lhs->size == rhs->size && lhs->inset == rhs->inset;
}

inline bool operator!=(const views_v1::ViewLayoutPtr& lhs,
                       const views_v1::ViewLayoutPtr& rhs) {
  return !(lhs == rhs);
}

}  // namespace views_v1

#endif  // LIB_UI_VIEWS_CPP_VIEWS_UTIL_H_
