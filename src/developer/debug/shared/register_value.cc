// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/register_value.h"

#include <string.h>

namespace debug {

__int128 RegisterValue::GetValue() const {
  __int128 result = 0;
  if (!data.empty())
    memcpy(&result, data.data(), std::min(sizeof(result), data.size()));
  return result;
}

}  // namespace debug
