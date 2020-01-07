// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/source_span.h"

namespace fidl {

std::string_view SourceSpan::SourceLine(SourceFile::Position* position_out) const {
  return source_file_->LineContaining(data(), position_out);
}

SourceFile::Position SourceSpan::position() const {
  SourceFile::Position pos;
  SourceLine(&pos);
  return pos;
}

std::string SourceSpan::position_str() const {
  std::string position(source_file_->filename());
  SourceFile::Position pos;
  SourceLine(&pos);
  position.push_back(':');
  position.append(std::to_string(pos.line));
  position.push_back(':');
  position.append(std::to_string(pos.column));
  return position;
}

}  // namespace fidl
