// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/serialization.h"

namespace debug {

Serializer& Serializer::operator|(std::string& val) {
  uint32_t size = static_cast<uint32_t>(val.size());
  *this | size;
  val.resize(size);
  SerializeBytes(val.data(), size);
  return *this;
}

}  // namespace debug
