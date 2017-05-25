// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/views/cpp/formatting.h"

#include <ostream>

namespace mozart {

std::ostream& operator<<(std::ostream& os, const ViewToken& value) {
  return os << "<V" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os, const ViewTreeToken& value) {
  return os << "<T" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os, const ViewInfo& value) {
  return os << "{scene_token=" << value.scene_token << "}";
}

std::ostream& operator<<(std::ostream& os, const ViewProperties& value) {
  return os << "{display_metrics=" << value.display_metrics
            << ", view_layout=" << value.view_layout << "}";
}

std::ostream& operator<<(std::ostream& os, const DisplayMetrics& value) {
  return os << "{device_pixel_ratio=" << value.device_pixel_ratio << "}";
}

std::ostream& operator<<(std::ostream& os, const ViewLayout& value) {
  return os << "{size=" << value.size << "}";
}

std::ostream& operator<<(std::ostream& os, const ViewInvalidation& value) {
  return os << "{properties=" << value.properties
            << ", container_flush_token=" << value.container_flush_token
            << ", scene_version=" << value.scene_version
            << ", frame_info=" << value.frame_info << "}";
}

}  // namespace mozart
