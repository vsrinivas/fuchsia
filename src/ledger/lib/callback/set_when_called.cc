// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "set_when_called.h"

#include <lib/fit/function.h>

namespace ledger {

fit::closure SetWhenCalled(bool* value) {
  *value = false;
  return [value] { *value = true; };
}

}  // namespace ledger
