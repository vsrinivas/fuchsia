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

#include <ktl/algorithm.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

// FILE* wrapper over a string.
class StringFile : public FILE {
 public:
  StringFile() = delete;
  StringFile(const StringFile &) = delete;
  StringFile operator=(const StringFile &) = delete;

  explicit StringFile(ktl::span<char> s) : FILE(this), buffer_(s) {}

  ~StringFile() = default;

  // Adds a null character at the end of the current buffer and returns a view
  // into the written section.
  ktl::span<char> take() &&;

  // Returns |str.size()| and writes as much of |str| as it would fit in the
  // buffer [offest_, size - 1) while reserving the last byte for null
  // character.
  int Write(ktl::string_view str);

  // Returns a region representing the currently used portion of the buffer.
  ktl::span<char> used_region() const {
    return (offset_ > 0) ? ktl::span<char>{buffer_.data(), offset_} : ktl::span<char>{};
  }

  // Returns a region representing the remaining unused space in the buffer,
  // _not_ including the space reserved for the final null character.
  ktl::span<char> available_region() const {
    const size_t available = ktl::max<size_t>(buffer_.size() - offset_, 1) - 1;
    return (available > 0) ? ktl::span<char>{buffer_.data() + offset_, available}
                            : ktl::span<char>{};
  }

  // Skip up to |amt| bytes in the buffer, advancing the write pointer as if we
  // had written data, but without actually changing the buffer.
  void Skip(size_t amt) {
    amt = ktl::min(amt, available_region().size());
    offset_ += amt;
  }

  // Provide two ways for users to convert the currently used buffer into a
  // string_view.  Given a string file |S|, users may say either:
  //
  // 1) S.as_string_view();   // or
  // 2) static_cast<ktl::string_view>(S);
  //
  // to perform the conversion.
  //
  ktl::string_view as_string_view() const {
    return (offset_ > 0) ? ktl::string_view{buffer_.data(), offset_} : ktl::string_view{};
  }

  explicit operator ktl::string_view() const { return this->as_string_view(); }

 private:
  ktl::span<char> buffer_;
  size_t offset_ = 0;
};

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STRING_FILE_H_
