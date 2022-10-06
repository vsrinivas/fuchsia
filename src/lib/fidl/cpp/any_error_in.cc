// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/any_error_in.h>

#include <cstdio>
#include <cstring>

namespace fidl::internal {

size_t ErrorsInBase::FormatImpl(const char* prelude, FormattingBuffer& buffer,
                                fit::inline_callback<size_t(char*, size_t)> display_error) {
  int num_would_write = 0;
  if (prelude != nullptr) {
    num_would_write = snprintf(buffer.begin(), buffer.size(), "%s", prelude);
    ZX_ASSERT(num_would_write > 0);
    if (static_cast<size_t>(num_would_write) >= buffer.size()) {
      return buffer.size() - 1;
    }
  }
  char* begin = buffer.begin() + num_would_write;
  size_t len = buffer.size() - num_would_write;
  return display_error(begin, len) + num_would_write;
}

const char* ErrorsInBase::kFrameworkErrorPrelude = nullptr;
const char* ErrorsInBase::kDomainErrorPrelude = "FIDL method domain error: ";

}  // namespace fidl::internal
