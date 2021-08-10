// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/register_test_support.h"

namespace debug {

RegisterValue CreateRegisterWithTestData(RegisterID id, size_t length) {
  RegisterValue reg;
  reg.id = id;

  reg.data.reserve(length);
  // So that we get the number backwards (0x0102...).
  size_t base = length;
  for (size_t i = 0; i < length; i++)
    reg.data.emplace_back(base - i);

  return reg;
}

}  // namespace debug
