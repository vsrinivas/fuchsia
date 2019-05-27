// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuzz_input.h"

#include <cstddef>
#include <cstdint>

#include <string.h>

namespace fuzzing {

const uint8_t* FuzzInput::TakeBytes(size_t size) {
  if (remaining_ < size) {
    return nullptr;
  }
  const uint8_t* out = data_;
  data_ += size;
  remaining_ -= size;
  return out;
}

bool FuzzInput::CopyBytes(uint8_t* out, size_t size) {
  if (remaining_ < size) {
    return false;
  }
  memcpy(out, data_, size);
  data_ += size;
  remaining_ -= size;
  return true;
}

}  // namespace fuzzing
