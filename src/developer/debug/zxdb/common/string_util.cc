// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/string_util.h"

namespace zxdb {

bool StringBeginsWith(std::string_view str, std::string_view begins_with) {
  if (begins_with.size() > str.size())
    return false;

  std::string_view source = str.substr(0, begins_with.size());
  return source == begins_with;
}

bool StringEndsWith(std::string_view str, std::string_view ends_with) {
  if (ends_with.size() > str.size())
    return false;

  std::string_view source =
      str.substr(str.size() - ends_with.size(), ends_with.size());
  return source == ends_with;
}

}  // namespace zxdb
