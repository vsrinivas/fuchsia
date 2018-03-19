// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/type_converters.h"

namespace fxl {

std::string TypeConverter<std::string, f1dl::StringPtr>::Convert(
    f1dl::StringPtr value) {
  return value;
}

f1dl::StringPtr TypeConverter<f1dl::StringPtr, std::string>::Convert(
    std::string value) {
  return f1dl::StringPtr(value);
}

}  // namespace fxl
