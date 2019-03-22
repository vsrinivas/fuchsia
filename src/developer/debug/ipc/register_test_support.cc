// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/register_test_support.h"

#include <string.h>

namespace debug_ipc {

namespace {

std::vector<uint8_t> CreateData(size_t length) {
  std::vector<uint8_t> data;
  data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++) {
    data.emplace_back(base - i);
  }
  return data;
}

}  // namespace

Register CreateRegister(RegisterID id, size_t length) {
  Register reg;
  reg.id = id;
  reg.data = std::vector<uint8_t>(length);
  // Zero out.
  memset(reg.data.data(), 0, length);
  return reg;
}

Register CreateRegisterWithData(RegisterID id, size_t length) {
  Register reg;
  reg.id = id;
  reg.data = CreateData(length);
  return reg;
}

Register CreateUint64Register(RegisterID id, uint64_t value) {
  Register reg;
  reg.id = id;
  reg.data = std::vector<uint8_t>(sizeof(uint64_t));
  *reinterpret_cast<uint64_t*>(reg.data.data()) = value;
  return reg;
}

}  // namespace debug_ipc
