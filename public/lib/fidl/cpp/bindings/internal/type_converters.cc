// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/type_converters.h"

namespace fxl {

std::string TypeConverter<std::string, f1dl::String>::Convert(
    f1dl::String value) {
  return value;
}

f1dl::String TypeConverter<f1dl::String, std::string>::Convert(
    std::string value) {
  return f1dl::String(value);
}

}  // namespace fxl
