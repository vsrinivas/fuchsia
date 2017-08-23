// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ftl/logging.h"

namespace escher {

enum class BindingType { kNone = 0, kConstant = 1 };

// Binds a property of a material to a source of data.
template <typename T>
class Binding {
 public:
  Binding() : type_(BindingType::kNone) {}
  ~Binding() {}

  static Binding Constant(const T& value) {
    Binding binding(BindingType::kConstant);
    binding.constant_value_ = value;
    return binding;
  }

  BindingType type() const { return type_; }

  const T& constant_value() const {
    FTL_DCHECK(type_ == BindingType::kConstant);
    return constant_value_;
  }

  // TODO(jeffbrown): Support binding to textures and simple expressions.
  // static Binding Texture(GLint texture_name);

 private:
  Binding(BindingType type) : type_(type) {}

  BindingType type_;
  T constant_value_;
};

template <typename T>
Binding<T> MakeConstantBinding(const T& value) {
  return Binding<T>::Constant(value);
}

}  // namespace escher
