// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TAGGED_DATA_BUILDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TAGGED_DATA_BUILDER_H_

#include <stdint.h>

#include <initializer_list>

#include "src/developer/debug/zxdb/common/tagged_data.h"
#include "src/lib/containers/cpp/array_view.h"

namespace zxdb {

// A builder for TaggedData that allows us to construct a buffer while hiding the implementation of
// the tags. This allows us to make guarantees about the tagged data and also gives is flexibility
// in the future to change how the tags are represented (for example, a more optimized range-based
// implementation, or possibly to extend to bit-level tags).
class TaggedDataBuilder {
 public:
  // Returns information on the accumulated data so far.
  bool empty() const { return data_.empty(); }
  size_t size() const { return data_.size(); }

  // Appends valid bytes to the builder.
  void Append(std::initializer_list<uint8_t> new_data) {
    // array_view's constructor can't take an initializer list because the initializer_list data is
    // a temporary that won't live beyond the constructor call. But we can guarantee the lifetime
    // of the initializer list data here.
    Append(containers::array_view<uint8_t>(new_data.begin(), new_data.end()));
  }
  void Append(containers::array_view<uint8_t> new_data);

  template <typename Iterator>
  void Append(Iterator begin, Iterator end) {
    Append(containers::array_view<uint8_t>(begin, end));
  }

  // Appends the given number of bytes marked as "unknown" to the buffer.
  void AppendUnknown(size_t count);

  // Destructively returns a TaggedData, resetting the builder to empty.
  TaggedData TakeData();

 private:
  TaggedData::DataBuffer data_;
  TaggedData::TagBuffer tags_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TAGGED_DATA_BUILDER_H_
