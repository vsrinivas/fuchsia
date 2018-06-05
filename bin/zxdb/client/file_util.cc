// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/file_util.h"

namespace zxdb {

fxl::StringView ExtractLastFileComponent(fxl::StringView path) {
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos)
    return path;
  return path.substr(last_slash + 1);
}

}  // namespace zxdb
