// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_STRING_BUFFER_H_
#define FBL_STRING_BUFFER_H_

#include <stdarg.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <string_view>

#include <fbl/string.h>

#include "string.h"

namespace fbl {
namespace internal {
size_t StringBufferAppendPrintf(char* dest, size_t remaining, const char* format, va_list ap);
}  // namespace internal

// A fixed-size buffer for assembling a string.
//
// fbl::StringBuffer is designed to resemble std::string except that it
// does not allocate heap storage.
//
// The buffer is sized to hold up to N characters plus a null-terminator.
template <size_t N>
class __OWNER(char) StringBuffer final {
 public:
  // Creates an empty string buffer.
  constexpr StringBuffer() : length_(0U) { data_[0] = 0; }

  // Creates a string buffer containing exactly one character and a null
  // terminator.  This constructor is constinit in practice so that it can be
  // used to initialize fdio's "cwd" path element without generating a dynamic
  // initializer.
  constexpr explicit StringBuffer(char c) : length_(1), data_{c, '\0'} { static_assert(N >= 1); }

  // Releases the string buffer.
  ~StringBuffer() = default;

  // Returns a pointer to the null-terminated contents of the string.
  char* data() { return data_; }
  const char* data() const { return data_; }
  const char* c_str() const { return data_; }

  // Returns the length of the string, excluding its null terminator.
  size_t length() const { return length_; }
  size_t size() const { return length_; }

  // Returns the length of the string, excluding its null terminator.
  bool empty() const { return length_ == 0U; }

  // Returns the capacity of the buffer.
  constexpr size_t capacity() const { return N; }

  // Character iterators, excluding the null terminator.
  char* begin() { return data(); }
  const char* begin() const { return data(); }
  const char* cbegin() const { return data(); }
  char* end() { return data() + length(); }
  const char* end() const { return data() + length(); }
  const char* cend() const { return data() + length(); }

  // Gets a reference to the character at the specified index.
  // Position must be in the range [0, length()].
  char& operator[](size_t pos) { return data_[pos]; }
  const char& operator[](size_t pos) const { return data_[pos]; }

  // Clears the string buffer.
  void Clear() {
    length_ = 0U;
    data_[0] = 0;
  }

  // Clears existing data from the buffer and sets the buffer to the new value, plus a null
  // terminator.
  void Set(std::string_view data) {
    ZX_DEBUG_ASSERT(data.size() < N);
    length_ = data.length();
    memcpy(data_, data.data(), data.length());
    data_[length_] = '\0';
  }

  // Resizes the string buffer.
  // If the current length is less than |count|, additional characters are appended
  // with the value |ch|.
  // If the current length is greater than |count|, the string is truncated.
  // |length| must be less than or equal to |N|.
  void Resize(size_t count, char ch = '\0') {
    ZX_DEBUG_ASSERT(count <= N);
    if (length_ < count)
      memset(data_ + length_, ch, count - length_);
    length_ = count;
    data_[length_] = 0;
  }

  // Remove the first |count| characters from the string buffer.
  void RemovePrefix(size_t count) {
    ZX_DEBUG_ASSERT(count <= N);
    length_ -= count;
    memmove(data_, data_ + count, length_);
    data_[length_] = '\0';
  }

  // Appends a single character.
  // The result is truncated if the appended content does not fit completely.
  StringBuffer& Append(char ch) {
    if (length_ < N) {
      data_[length_++] = ch;
      data_[length_] = 0;
    }
    return *this;
  }

  // Appends content to the string buffer from a null-terminated C string.
  // The result is truncated if the appended content does not fit completely.
  // |data| must not be null.
  StringBuffer& Append(const char* data) {
    Append(data, std::string_view(data).size());
    return *this;
  }

  // Appends content to the string buffer from a character array of given length.
  // The result is truncated if the appended content does not fit completely.
  // |data| must not be null.
  StringBuffer& Append(const char* data, size_t length) {
    AppendInternal(data, length);
    return *this;
  }

  // Appends content to the string buffer from a string piece.
  // The result is truncated if the appended content does not fit completely.
  StringBuffer& Append(std::string_view piece) {
    AppendInternal(piece.data(), piece.length());
    return *this;
  }

  // Appends content to the string buffer from another string.
  // The result is truncated if the appended content does not fit completely.
  StringBuffer& Append(const fbl::String& other) {
    AppendInternal(other.data(), other.length());
    return *this;
  }

  // Appends |printf()|-like input.
  // The result is truncated if the appended content does not fit completely.
  StringBuffer& AppendPrintf(const char* format, ...) __PRINTFLIKE(2, 3) {
    va_list ap;
    va_start(ap, format);
    AppendVPrintf(format, ap);
    va_end(ap);
    return *this;
  }

  // Appends |vprintf()|-like input using a |va_list|.
  // The result is truncated if the appended content does not fit completely.
  StringBuffer& AppendVPrintf(const char* format, va_list ap) {
    length_ += internal::StringBufferAppendPrintf(data_ + length_, N - length_, format, ap);
    return *this;
  }

  // Gets the buffer's contents as a string.
  fbl::String ToString() const { return fbl::String(data(), length()); }

  // Creates a string piece backed by the string.
  // The string piece does not take ownership of the data so the string
  // must outlast the string piece.
  operator std::string_view() const { return {data(), length()}; }

 private:
  void AppendInternal(const char* data, size_t length) {
    size_t remaining = N - length_;
    if (length > remaining)
      length = remaining;
    memcpy(data_ + length_, data, length);
    length_ += length;
    data_[length_] = 0;
  }

  size_t length_ = 0U;
  char data_[N + 1U];
};

}  // namespace fbl

#endif  // FBL_STRING_BUFFER_H_
