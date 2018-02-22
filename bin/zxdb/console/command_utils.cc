// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include <inttypes.h>
#include <stdio.h>

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

Err StringToUint64(const std::string& s, uint64_t* out) {
  if (s.size() > 2 && s[0] == '0' && s[1] == 'x') {
    if (sscanf(&s.c_str()[2], "%" PRIx64, out) == 1)
      return Err();
  }
  if (sscanf(s.c_str(), "%" PRIu64, out) == 1)
    return Err();
  return Err(ErrType::kInput, "Invalid number \"" + s + "\".");
}

}  // namespace zxdb
