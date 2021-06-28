// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/tagged_data.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/developer/debug/zxdb/common/string_util.h"

namespace zxdb {

TaggedData::TaggedData(DataBuffer bytes) : bytes_(std::move(bytes)) {}

TaggedData::TaggedData(DataBuffer buf, TagBuffer states)
    : bytes_(std::move(buf)), tags_(std::move(states)) {
  FX_DCHECK(tags_.empty() || tags_.size() == bytes_.size());

  // Enforce that entirely-valid implies an empty state vector.
  if (!tags_.empty()) {
    if (std::find_if(tags_.begin(), tags_.end(), [](Tag s) { return s != kValid; }) == tags_.end())
      tags_.clear();
  }
}

bool TaggedData::RangeIsEntirely(size_t begin, size_t length, Tag tag) {
  FX_DCHECK(begin + length <= bytes_.size());

  if (tags_.empty())
    return tag == kValid;

  FX_DCHECK(bytes_.size() == tags_.size());
  for (size_t i = begin; i < begin + length; i++) {
    if (tags_[i] != tag)
      return false;
  }
  return true;
}

bool TaggedData::RangeContains(size_t begin, size_t length, Tag tag) {
  FX_DCHECK(begin + length <= bytes_.size());

  if (tags_.empty())
    return tag == kValid;

  FX_DCHECK(bytes_.size() == tags_.size());
  for (size_t i = begin; i < begin + length; i++) {
    if (tags_[i] == tag)
      return true;
  }
  return false;
}

bool TaggedData::operator==(const TaggedData& other) const {
  return bytes_ == other.bytes_ && tags_ == other.tags_;
}

bool TaggedData::operator!=(const TaggedData& other) const { return !operator==(other); }

std::optional<TaggedData> TaggedData::Extract(size_t offset, size_t length) const {
  if (offset + length > size())
    return std::nullopt;

  if (tags_.empty()) {
    // Common-case of entirely valid buffer.
    return TaggedData(DataBuffer(bytes_.begin() + offset, bytes_.begin() + (offset + length)));
  }

  // Extract a subregion of the states buffer. The constructor will "fix" the extracted region
  // if it's entirely valid.
  FX_DCHECK(tags_.size() == bytes_.size());
  return TaggedData(DataBuffer(bytes_.begin() + offset, bytes_.begin() + (offset + length)),
                    TagBuffer(tags_.begin() + offset, tags_.begin() + (offset + length)));
}

std::string TaggedData::ToString() const {
  std::ostringstream out;

  size_t index = 0;
  while (index < bytes_.size()) {
    for (size_t col = 0; col < 16 && index < bytes_.size(); col++, index++) {
      if (col == 8) {
        // Center separator.
        out << "   ";
      } else if (col > 0) {
        out << " ";
      }

      if (tags_.empty() || tags_[index] == kValid) {
        out << to_hex_string(bytes_[index], 2, false);
      } else {
        out << "??";
      }
    }

    out << "\n";
  }

  return out.str();
}

}  // namespace zxdb
