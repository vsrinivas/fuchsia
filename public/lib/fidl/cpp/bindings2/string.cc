// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/string.h"

#include <string.h>

namespace fidl {
namespace {

bool PutAt(Builder* builder, StringView* view, const char* str, size_t size) {
  char* data = builder->NewArray<char>(size);
  if (!data)
    return false;
  view->set_data(data);
  view->set_size(size);
  memcpy(data, str, size);
  return true;
}

}  // namespace

StringPtr::StringPtr() : is_null_(true) {}

StringPtr::StringPtr(std::string str) : str_(std::move(str)), is_null_(false) {}

StringPtr::~StringPtr() = default;

StringPtr::StringPtr(StringPtr&& other)
  : str_(std::move(other.str_)), is_null_(other.is_null_) {}

StringPtr& StringPtr::operator=(StringPtr&& other) {
  str_ = std::move(other.str_);
  is_null_ = other.is_null_;
  return *this;
}

StringPtr StringPtr::Take(StringView* view) {
  return view->is_null() ?
      StringPtr() : StringPtr(std::string(view->data(), view->size()));
}

bool PutAt(Builder* builder, StringView* view, StringPtr* string) {
  if (string->is_null())
    return true;
  return PutAt(builder, view, (*string)->data(), (*string)->size());
}

StringView* Build(Builder* builder, const char* string, size_t size) {
  StringView* view = builder->New<StringView>();
  if (view && PutAt(builder, view, string, size))
    return view;
  return nullptr;
}

StringView* Build(Builder* builder, const char* string) {
  return Build(builder, string, strlen(string));
}

StringView* Build(Builder* builder, StringView string) {
  return Build(builder, string.data(), string.size());
}

StringView* Build(Builder* builder, const std::string& string) {
  return Build(builder, string.data(), string.size());
}

StringView* Build(Builder* builder, const StringPtr& string) {
  if (!string)
    return builder->New<StringView>();
  return Build(builder, *string);
}

}  // namespace fidl
