// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/convert/convert.h"

#include <leveldb/slice.h>
#include <string.h>

namespace convert {

mojo::Array<uint8_t> ToArray(const ExtendedStringView& value) {
  mojo::Array<uint8_t> result = mojo::Array<uint8_t>::New(value.size());
  memcpy(result.data(), value.data(), value.size());
  return result;
}

std::string ToString(const ExtendedStringView& bytes) {
  return std::string(bytes.data(), bytes.size());
}

}  // namespace convert
