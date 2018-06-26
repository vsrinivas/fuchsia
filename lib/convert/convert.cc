// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/convert/convert.h"

#include <string.h>

#include <leveldb/slice.h>

namespace convert {

namespace {
const char kHexDigits[] = "0123456789ABCDEF";
}

fidl::VectorPtr<uint8_t> ExtendedStringView::ToArray() {
  fidl::VectorPtr<uint8_t> result = fidl::VectorPtr<uint8_t>::New(size());
  memcpy(result->data(), data(), size());
  return result;
}

flatbuffers::Offset<flatbuffers::Vector<uint8_t>>
ExtendedStringView::ToFlatBufferVector(
    flatbuffers::FlatBufferBuilder* builder) {
  return builder->CreateVector(reinterpret_cast<const unsigned char*>(data()),
                               size());
}

std::string ExtendedStringView::ToHex() {
  std::string result;
  result.reserve(size() * 2);
  for (unsigned char c : *this) {
    result.push_back(kHexDigits[c >> 4]);
    result.push_back(kHexDigits[c & 0xf]);
  }
  return result;
}

fidl::VectorPtr<uint8_t> ToArray(ExtendedStringView value) {
  return value.ToArray();
}

std::string ToString(ExtendedStringView value) {
  return value.ToString();
}

flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferVector(
    flatbuffers::FlatBufferBuilder* builder,
    ExtendedStringView value) {
  return value.ToFlatBufferVector(builder);
}

std::string ToHex(ExtendedStringView value) {
  return value.ToHex();
}

}  // namespace convert
