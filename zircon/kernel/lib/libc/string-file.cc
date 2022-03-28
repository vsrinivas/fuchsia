// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string-file.h>

#include <ktl/algorithm.h>
#include <ktl/string_view.h>

#include <ktl/enforce.h>

int StringFile::Write(ktl::string_view str) {
  // Last element is reserved for '\0' character.
  if (!buffer_.empty()) {
    offset_ += str.copy(buffer_.data() + offset_, buffer_.size() - offset_ - 1);
  }
  return static_cast<int>(str.size());
}

ktl::span<char> StringFile::take() && {
  if (!buffer_.empty()) {
    buffer_[offset_] = '\0';
    return buffer_.subspan(0, offset_ + 1);
  }
  return {};
}
