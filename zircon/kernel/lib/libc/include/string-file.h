// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STRING_FILE_H_
#define ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STRING_FILE_H_

#ifdef __cplusplus
#include <stdio.h>

#include <ktl/span.h>
#include <ktl/string_view.h>

// File* wrapper over a string.
class StringFile {
 public:
  StringFile() = delete;

  explicit StringFile(ktl::span<char> s) : buffer_(s) {}

  ~StringFile() = default;

  FILE* file() { return &file_; }

  // Adds a null character at the end of the current buffer and returns a view into the written
  // section.
  ktl::span<char> take() &&;

  // Returns |str.size()| and writes as much of |str| as it would fit in the buffer [offest_, size -
  // 1) while reserving the last byte for null character.
  int Write(ktl::string_view str);

 private:
  FILE file_{this};
  ktl::span<char> buffer_;
  size_t offset_ = 0;
};

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STRING_FILE_H_
