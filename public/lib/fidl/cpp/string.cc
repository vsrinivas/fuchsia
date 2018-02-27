// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/string.h"

#include <string.h>

#include "lib/fidl/cpp/encoder.h"

namespace fidl {

StringPtr::StringPtr() : is_null_(true) {}

StringPtr::StringPtr(std::string str) : str_(std::move(str)), is_null_(false) {}

StringPtr::StringPtr(const char* str)
    : str_(str ? std::string(str) : std::string()), is_null_(!str) {}

StringPtr::StringPtr(const char* str, size_t length)
    : str_(str ? std::string(str, length) : std::string()), is_null_(!str) {}

StringPtr::~StringPtr() = default;

StringPtr::StringPtr(StringPtr&& other)
    : str_(std::move(other.str_)), is_null_(other.is_null_) {}

StringPtr& StringPtr::operator=(StringPtr&& other) {
  str_ = std::move(other.str_);
  is_null_ = other.is_null_;
  return *this;
}

StringPtr StringPtr::Take(StringView* view) {
  return view->is_null() ? StringPtr()
                         : StringPtr(std::string(view->data(), view->size()));
}

void StringPtr::Encode(Encoder* encoder, size_t offset) {
  fidl_string_t* string = encoder->GetPtr<fidl_string_t>(offset);
  if (is_null()) {
    string->size = 0u;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  } else {
    string->size = str_.size();
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    size_t base = encoder->Alloc(str_.size());
    char* payload = encoder->GetPtr<char>(base);
    memcpy(payload, str_.data(), str_.size());
  }
}

}  // namespace fidl
