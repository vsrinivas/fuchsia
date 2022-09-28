// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_REGISTER_VALUE_H_
#define SRC_DEVELOPER_DEBUG_SHARED_REGISTER_VALUE_H_

#include <inttypes.h>

#include <vector>

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/shared/serialization.h"

namespace debug {

struct RegisterValue {
  RegisterValue() = default;
  RegisterValue(RegisterID rid, std::vector<uint8_t> d) : id(rid), data(std::move(d)) {}

  // Constructs from a size and a pointed-to data buffer in machine-endianness.
  RegisterValue(RegisterID rid, size_t byte_size, const void* bytes) : id(rid) {
    data.resize(byte_size);
    memcpy(data.data(), bytes, byte_size);
  }

  // Constructs a sized value for the current platform.
  RegisterValue(RegisterID rid, uint64_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(data.data(), &val, sizeof(val));
  }
  RegisterValue(RegisterID rid, uint32_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(data.data(), &val, sizeof(val));
  }
  RegisterValue(RegisterID rid, uint16_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(data.data(), &val, sizeof(val));
  }
  RegisterValue(RegisterID rid, uint8_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(data.data(), &val, sizeof(val));
  }

  // Retrieves the low up-to-128 bits of the register value as a number.
  __int128 GetValue() const;

  // Comparisons (primarily for tests).
  bool operator==(const RegisterValue& other) const { return id == other.id && data == other.data; }
  bool operator!=(const RegisterValue& other) const { return !operator==(other); }

  RegisterID id = RegisterID::kUnknown;

  // This data is stored in the architecture's native endianness
  // (eg. the result of running memcpy over the data).
  std::vector<uint8_t> data;

  void Serialize(Serializer& ser, uint32_t ver) { ser | id | data; }
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_REGISTER_VALUE_H_
