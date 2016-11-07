// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/convert/convert.h"

#include <leveldb/slice.h>
#include <string.h>

namespace convert {

mojo::Array<uint8_t> ExtendedStringView::ToArray() {
  mojo::Array<uint8_t> result = mojo::Array<uint8_t>::New(size());
  memcpy(result.data(), data(), size());
  return result;
}

mojo::Array<uint8_t> ToArray(ExtendedStringView value) {
  return value.ToArray();
}

std::string ToString(ExtendedStringView value) {
  return value.ToString();
}

}  // namespace convert
