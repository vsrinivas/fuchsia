// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/strings/trim.h"

namespace ftl {

ftl::StringView TrimString(ftl::StringView str, ftl::StringView chars_to_trim) {
  size_t start_index = str.find_first_not_of(chars_to_trim);
  if (start_index == ftl::StringView::npos) {
    return ftl::StringView();
  }
  size_t end_index = str.find_last_not_of(chars_to_trim);
  return str.substr(start_index, end_index - start_index + 1);
}

}  // namespace ftl
