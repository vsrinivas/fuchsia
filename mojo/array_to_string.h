// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_MOJO_ARRAY_TO_STRING_H_
#define APPS_MODULAR_MOJO_ARRAY_TO_STRING_H_

#include <string>

#include "mojo/public/cpp/bindings/array.h"

namespace modular {

std::string to_string(const mojo::Array<uint8_t>& data) {
  std::string ret;
  ret.reserve(data.size());

  for (uint8_t val : data) {
    ret += std::to_string(val);
  }

  return ret;
}

mojo::Array<uint8_t> to_array(const std::string& val) {
  mojo::Array<uint8_t> ret;
  for (char c : val) {
    ret.push_back(c);
  }
  return ret;
}

}  // namespace modular

#endif
