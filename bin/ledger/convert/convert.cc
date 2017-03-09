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

flatbuffers::Offset<ByteStorage> ExtendedStringView::ToByteStorage(
    flatbuffers::FlatBufferBuilder* builder) {
  return CreateByteStorage(
      *builder, builder->CreateVector(
                    reinterpret_cast<const unsigned char*>(data()), size()));
}

fidl::Array<uint8_t> ToArray(ExtendedStringView value) {
  return value.ToArray();
}

std::string ToString(ExtendedStringView value) {
  return value.ToString();
}

flatbuffers::Offset<ByteStorage> ToByteStorage(
    flatbuffers::FlatBufferBuilder* builder,
    ExtendedStringView value) {
  return value.ToByteStorage(builder);
}

}  // namespace convert
