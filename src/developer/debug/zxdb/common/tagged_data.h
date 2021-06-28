// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TAGGED_DATA_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TAGGED_DATA_H_

#include <optional>
#include <vector>

namespace zxdb {

// A data buffer with per-byte tags for validity. This allows us to express that certain bytes may
// be valid while others might be unknown. This can happen for optimized code where, for example,
// some portions of a struct are kept in registers so can be known, but other portions of the struct
// are optimized out.
class TaggedData {
 public:
  enum Tag : uint8_t {
    kValid,
    kUnknown,
  };

  using DataBuffer = std::vector<uint8_t>;
  using TagBuffer = std::vector<Tag>;

  // Constructs a buffer of entirely valid data. To construct one with different regions of valid
  // and invalid, use the TaggedDataBuilder.
  explicit TaggedData(DataBuffer bytes = DataBuffer());

  const DataBuffer& bytes() const { return bytes_; }

  // NOTE: there is no accessor for the tag buffer to allow us to change the format in the future.
  // If this is used for very large things, we may want to go for a range-based representation.
  // There is also some possibility that it will need to represent bit validity in the future.
  // If additional querying is needed, add functions to query the state of a given range rather than
  // exposing the TagBuffer externally.

  // Returns true if the given range (which is asserted to be valid) satisfies the condition.
  bool RangeIsEntirely(size_t begin, size_t length, Tag tag);
  bool RangeContains(size_t begin, size_t length, Tag tag);

  size_t size() const { return bytes_.size(); }
  bool empty() const { return bytes_.empty(); }

  bool operator==(const TaggedData& other) const;
  bool operator!=(const TaggedData& other) const;

  // Returns true if the entire buffer is valid.
  bool all_valid() const { return tags_.empty(); }

  // Extracts a subrange of the buffer. Returns nullopt if the range falls outside of the data
  // range.
  std::optional<TaggedData> Extract(size_t offset, size_t length) const;

  std::string ToString() const;

 private:
  friend class TaggedDataBuilder;

  // When the Tag vector is empty (should be the common-case), all bytes are marked valid.
  explicit TaggedData(DataBuffer bytes, TagBuffer states);

  DataBuffer bytes_;

  // Empty if all bytes are valid. Otherwise, the same size as bytes_ with per-byte validity.
  TagBuffer tags_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TAGGED_DATA_H_
