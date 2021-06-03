// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SOURCE_SPAN_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SOURCE_SPAN_H_

#include <cassert>
#include <cstdint>
#include <string_view>

#include "source_manager.h"

namespace fidl {

// A SourceSpan represents a span of a source file. It consists of a std::string_view, and a
// reference to the SourceFile that is backing the std::string_view.

class SourceSpan {
 public:
  // TODO(fxbug.dev/70427): we use a string, offset pair since this is persistent across
  // SourceSpans that come from different SourceFiles of the same content (the initial
  // data is stored during compilation of one copy, and fidlconv uses another during conversion).
  using Key = std::pair<std::string, size_t>;

  constexpr SourceSpan(std::string_view data, const SourceFile& source_file)
      : data_(data), source_file_(&source_file) {}

  constexpr SourceSpan() : data_(std::string_view()), source_file_(nullptr) {}

  constexpr bool valid() const { return source_file_ != nullptr; }

  constexpr const std::string_view& data() const { return data_; }
  constexpr const SourceFile& source_file() const {
    assert(valid());
    return *source_file_;
  }

  std::string_view SourceLine(SourceFile::Position* position_out) const;

  SourceFile::Position position() const;
  std::string position_str() const;

  // identity
  constexpr bool operator==(const SourceSpan& rhs) const {
    return data_.data() == rhs.data_.data() && data_.size() == rhs.data_.size();
  }

  // supports sorted sets or ordering by SourceSpan, based on filename,
  // start position, and then end position.
  inline bool operator<(const SourceSpan& rhs) const {
    return (source_file_->filename() < rhs.source_file_->filename() ||
            (source_file_ == rhs.source_file_ &&
             (data_.data() < rhs.data_.data() ||
              (data_.data() == rhs.data_.data() && (data_.size() < rhs.data_.size())))));
  }

  // TODO(fxbug.dev/70427): remove this with the converter
  Key ToKey() const;

 private:
  std::string_view data_;
  const SourceFile* source_file_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SOURCE_SPAN_H_
