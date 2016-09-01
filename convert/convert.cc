// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/convert/convert.h"

#include <leveldb/slice.h>
#include <string.h>

namespace convert {

BytesReference::BytesReference(const mojo::Array<uint8_t>& array)
    : data_(reinterpret_cast<const char*>(array.data())), size_(array.size()) {}

BytesReference::BytesReference(const leveldb::Slice& slice)
    : data_(slice.data()), size_(slice.size()) {}

BytesReference::BytesReference(const std::string& string)
    : data_(string.data()), size_(string.size()) {}

BytesReference::BytesReference(const char* c_string)
    : data_(c_string), size_(strlen(c_string)) {}

const leveldb::Slice ToSlice(const BytesReference& value) {
  return leveldb::Slice(value.data(), value.size());
}

mojo::Array<uint8_t> ToArray(const BytesReference& value) {
  mojo::Array<uint8_t> result = mojo::Array<uint8_t>::New(value.size());
  memcpy(result.data(), value.data(), value.size());
  return result;
}

std::string ToString(const BytesReference& bytes) {
  return std::string(bytes.data(), bytes.size());
}

}  // namespace convert
