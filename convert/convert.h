// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONVERT_CONVERT_H_
#define CONVERT_CONVERT_H_

#include <leveldb/db.h>
#include <string>

#include "mojo/public/cpp/bindings/array.h"

namespace convert {

// Provides conversions between mojo::Array, leveldb::Slice and std::string
// representations of a data blob.

// This class doesn't take ownership of the data used to construct it. The data
// must outlive it. It is used to allow transparent handling of mojo arrays,
// leveldb slices and strings.
class BytesReference {
 public:
  BytesReference(const mojo::Array<uint8_t>& array);
  BytesReference(const leveldb::Slice& slice);
  BytesReference(const std::string& string);
  BytesReference(const char* c_string);

  const char* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const char* const data_;
  const size_t size_;
};

// Returns the representation of the given value in LevelDB.
const leveldb::Slice ToSlice(const BytesReference& value);

// Returns the mojo::Array representation of the given value.
mojo::Array<uint8_t> ToArray(const BytesReference& value);

// Returns the std::string representation of the given value.
std::string ToString(const BytesReference& value);

}  // namespace convert

#endif  // CONVERT_CONVERT_H_
