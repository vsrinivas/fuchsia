// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_SERIALIZATION_H_
#define SRC_DEVELOPER_DEBUG_SHARED_SERIALIZATION_H_

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// This is a simple "serialization" solution for the debug_ipc.
//
// It utilizes template and function overloading to avoid writing serialization and deserialization
// functions twice.
//
// To add serialization support for a type, either
//
//   - For classes owned by us, provide a templated member function
//     `void Serialize(Serializer& ser, uint32_t ver)` in the class.
//   - For classes not owned by us / non-class types, provide a templated global function
//     `SerializeWithSerializer(Serializer& ser, Type& object)`.
//
// These functions will be used by both serialization and deserialization, although the names only
// contain "serialize". In another word, these functions could also write to the object.

namespace debug {

// To implement a Serializer/Deserializer, inherit this interface.
class Serializer {
 public:
  template <typename T>
  Serializer& operator|(T& val) {
    SerializeWithSerializer(*this, val);
    return *this;
  }

  // Returns the desired version for serialization.
  virtual uint32_t GetVersion() const = 0;

  // Reads or writes bytes.
  virtual void SerializeBytes(void* data, uint32_t size) = 0;
};

template <typename T>
auto SerializeWithSerializer(Serializer& ser, T& val) -> decltype(val.Serialize(ser, 0)) {
  val.Serialize(ser, ser.GetVersion());
}

template <typename Integer>
auto SerializeWithSerializer(Serializer& ser, Integer& val)
    -> std::enable_if_t<std::is_integral_v<Integer>, void> {
  ser.SerializeBytes(&val, sizeof(val));
}

template <typename Enum>
auto SerializeWithSerializer(Serializer& ser, Enum& val)
    -> std::enable_if_t<std::is_enum_v<Enum>, void> {
  uint32_t v = static_cast<uint32_t>(val);
  SerializeWithSerializer(ser, v);
  val = static_cast<Enum>(v);
}

template <typename T>
void SerializeWithSerializer(Serializer& ser, std::optional<T>& val) {
  uint32_t has_value = val.has_value();
  SerializeWithSerializer(ser, has_value);
  if (has_value) {
    if (!val.has_value()) {
      val.emplace();
    }
    SerializeWithSerializer(ser, val.value());
  } else if (val.has_value()) {
    val.reset();
  }
}

template <typename T>
void SerializeWithSerializer(Serializer& ser, std::vector<T>& val) {
  uint32_t size = static_cast<uint32_t>(val.size());
  SerializeWithSerializer(ser, size);
  val.resize(size);
  for (uint32_t i = 0; i < size; i++) {
    SerializeWithSerializer(ser, val[i]);
  }
}

void SerializeWithSerializer(Serializer& ser, std::string& val);

}  // namespace debug

namespace debug_ipc {

using Serializer = debug::Serializer;  // for convenience.

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_SERIALIZATION_H_
