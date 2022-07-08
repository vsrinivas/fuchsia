// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FORMATTING_FORMATTING_H_
#define SRC_CAMERA_LIB_FORMATTING_FORMATTING_H_

#include <ostream>
#include <sstream>
#include <string>

// This function serializes an object into a format suitable for logging or inspection.
// Argument-dependent lookup (ADL) is used to avoid ambity with the generic std::operator<<
// function. The specific form it takes is not stable. Library-specific specializations are
// implemented in the adjacent cc file.
#define CAMERA_DECL_ADL_STREAM_OPERATOR(ns)               \
  namespace ns {                                          \
  template <typename T>                                   \
  std::ostream& operator<<(std::ostream& os, const T& x); \
  }
CAMERA_DECL_ADL_STREAM_OPERATOR(fuchsia::camera2)
CAMERA_DECL_ADL_STREAM_OPERATOR(fuchsia::camera2::hal)
CAMERA_DECL_ADL_STREAM_OPERATOR(fuchsia::sysmem)
#undef CAMERA_DECL_ADL_STREAM_OPERATOR

namespace camera::formatting {

template <typename T>
std::string ToString(const T& x) {
  std::ostringstream oss;
  oss << x;
  return oss.str();
}

}  // namespace camera::formatting

#endif  // SRC_CAMERA_LIB_FORMATTING_FORMATTING_H_
