// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/common/string_util.h"

namespace zxdb {

bool StringEndsWith(fxl::StringView str, fxl::StringView ends_with) {
  if (ends_with.size() > str.size())
    return false;

  fxl::StringView source =
      str.substr(str.size() - ends_with.size(), ends_with.size());
  return source == ends_with;
}

}  // namespace zxdb
