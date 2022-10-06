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
//   - For classes owned by us, provide a function `void Serialize(Serializer& ser, uint32_t ver)`
//     in the class.
//   - For classes not owned by us / non-class types, provide a function
//     `Serializer& Serializer::operator|(Type& object)`.
//
// These functions will be used by both serialization and deserialization, although the names only
// contain "serialize". In another word, these functions could also write to the object.

namespace debug {

// To implement a Serializer/Deserializer, inherit this interface.
class Serializer {
 public:
  template <typename T>
  auto operator|(T& val)
      -> std::enable_if_t<std::is_void_v<decltype(val.Serialize(*this, 0))>, Serializer&> {
    val.Serialize(*this, GetVersion());
    return *this;
  }

  template <typename Integer>
  auto operator|(Integer& val) -> std::enable_if_t<std::is_integral_v<Integer>, Serializer&> {
    SerializeBytes(&val, sizeof(val));
    return *this;
  }

  template <typename Enum>
  auto operator|(Enum& val) -> std::enable_if_t<std::is_enum_v<Enum>, Serializer&> {
    uint32_t v = static_cast<uint32_t>(val);
    *this | v;
    val = static_cast<Enum>(v);
    return *this;
  }

  template <typename T>
  Serializer& operator|(std::optional<T>& val) {
    uint32_t has_value = val.has_value();
    *this | has_value;
    if (has_value) {
      if (!val.has_value()) {
        val.emplace();
      }
      *this | val.value();
    } else if (val.has_value()) {
      val.reset();
    }
    return *this;
  }

  template <typename T>
  Serializer& operator|(std::vector<T>& val) {
    uint32_t size = static_cast<uint32_t>(val.size());
    *this | size;
    val.resize(size);
    for (uint32_t i = 0; i < size; i++) {
      *this | val[i];
    }
    return *this;
  }

  Serializer& operator|(std::string& val);

  // Returns the desired version for serialization.
  virtual uint32_t GetVersion() const = 0;

  // Reads or writes bytes.
  virtual void SerializeBytes(void* data, uint32_t size) = 0;
};

}  // namespace debug

namespace debug_ipc {

using Serializer = debug::Serializer;  // for convenience.

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_SERIALIZATION_H_
