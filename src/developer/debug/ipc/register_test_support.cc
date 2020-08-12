// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/register_test_support.h"

namespace debug_ipc {

Register CreateRegisterWithTestData(RegisterID id, size_t length) {
  Register reg;
  reg.id = id;

  reg.data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++)
    reg.data.emplace_back(base - i);

  return reg;
}

}  // namespace debug_ipc
