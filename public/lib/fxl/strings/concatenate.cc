// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/strings/concatenate.h"

namespace fxl {

std::string Concatenate(std::initializer_list<fxl::StringView> string_views) {
  std::string result;
  size_t result_size = 0;
  for (const fxl::StringView& string_view : string_views) {
    result_size += string_view.size();
  }
  result.reserve(result_size);
  for (const fxl::StringView& string_view : string_views) {
    result.append(string_view.data(), string_view.size());
  }
  return result;
}

}  // namespace fxl
