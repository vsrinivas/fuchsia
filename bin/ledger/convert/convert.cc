// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/convert/convert.h"

#include <leveldb/slice.h>
#include <string.h>

namespace convert {

fidl::Array<uint8_t> ExtendedStringView::ToArray() {
  fidl::Array<uint8_t> result = fidl::Array<uint8_t>::New(size());
  memcpy(result.data(), data(), size());
  return result;
}

fidl::Array<uint8_t> ToArray(ExtendedStringView value) {
  return value.ToArray();
}

std::string ToString(ExtendedStringView value) {
  return value.ToString();
}

}  // namespace convert
