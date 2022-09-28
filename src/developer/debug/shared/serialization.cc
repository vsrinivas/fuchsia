// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/serialization.h"

namespace debug {

void SerializeWithSerializer(Serializer& ser, std::string& val) {
  uint32_t size = static_cast<uint32_t>(val.size());
  SerializeWithSerializer(ser, size);
  val.resize(size);
  ser.SerializeBytes(val.data(), size);
}

}  // namespace debug
