// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_STRING_H_
#define LIB_FIDL_CPP_STRING_H_

#include <fidl/cpp/builder.h>
#include <fidl/cpp/string_view.h>

#include <string>
#include <utility>

#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/traits.h"

namespace fidl {

// A representation of a FIDL string that owns the memory for the string.
//
// A StringPtr has three states: (1) null, (2) empty, (3) contains a string. In
// the first and second states, operations that return an std::string return
// the empty std::string. The null and empty states can be distinguished using
// the |is_null| and |operator bool| methods.
class StringPtr {
 public:
  StringPtr();
  StringPtr(std::string str);
  StringPtr(const char* str);
  StringPtr(const char* str, size_t length);
  ~StringPtr();

  StringPtr(const StringPtr&) = delete;
  StringPtr& operator=(const StringPtr&) = delete;

  StringPtr(StringPtr&& other);

  StringPtr& operator=(StringPtr&& other);

  // Accesses the underlying std::string object.
  //
  // The returned string will be empty if the StringPtr is either null or empty.
  std::string& get() { return str_; }
  const std::string& get() const { return str_; }

  // Stores the given std::string in this StringPtr.
  //
  // After this method returns, the StringPtr is non-null.
  void reset(std::string str) {
    str_ = std::move(str);
    is_null_ = false;
  }

  void swap(StringPtr& other) {
    using std::swap;
    swap(str_, other.str_);
    swap(is_null_, other.is_null_);
  }

  // Whether this StringPtr is null.
  //
  // The null state is separate from the empty state.
  bool is_null() const { return is_null_; }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null_; }

  // Provides access to the underlying std::string.
  std::string* operator->() { return &str_; }
  const std::string* operator->() const { return &str_; }

  // Provides access to the underlying std::string.
  std::string& operator*() { return str_; }
  const std::string& operator*() const { return str_; }

  void Encode(Encoder* encoder, size_t offset);
  static void Decode(Decoder* decoder, StringPtr* value, size_t offset);

 private:
  std::string str_;
  bool is_null_;
};

template <>
struct CodingTraits<StringPtr>
    : public EncodableCodingTraits<StringPtr, sizeof(fidl_string_t)> {};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_STRING_H_
