// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/set_when_called.h"

namespace ledger {

fxl::Closure SetWhenCalled(bool* value) {
  *value = false;
  return [value] { *value = true; };
}

}  // namespace test
