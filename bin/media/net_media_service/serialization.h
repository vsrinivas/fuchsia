// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

namespace media {

// Used to wrap values that are optional in a serialized message.
template <typename T>
struct SerializationOptionalWrapper {
  explicit SerializationOptionalWrapper(T& t) : t_(t) {}
  T& t_;
};

// Applies a |SerializationOptionalWrapper| to a value.
template <typename T>
SerializationOptionalWrapper<T> Optional(T& t) {
  return SerializationOptionalWrapper<T>(t);
}

// Serializes values into a byte vector.
class Serializer {
 public:
  template <typename T>
  static std::vector<uint8_t> Serialize(const T& t) {
    Serializer serializer;
    serializer << t;
    return serializer.GetSerialMessage();
  }

  Serializer();
  ~Serializer();

  // Gets the serial message and resets this |Serializer|.
  std::vector<uint8_t> GetSerialMessage();

  // Puts |count| bytes from |source| into the serial message.
  void PutBytes(size_t count, const void* source);

  Serializer& operator<<(bool value);
  Serializer& operator<<(uint8_t value);
  Serializer& operator<<(uint16_t value);
  Serializer& operator<<(uint32_t value);
  Serializer& operator<<(uint64_t value);
  Serializer& operator<<(int8_t value);
  Serializer& operator<<(int16_t value);
  Serializer& operator<<(int32_t value);
  Serializer& operator<<(int64_t value);

 private:
  std::vector<uint8_t> serial_message_;
};

// The |Optional| function allows for the serialization of values that may be
// null (e.g. serializer << Optional(possiblyNullPointer)).
template <typename T>
Serializer& operator<<(Serializer& serializer,
                       SerializationOptionalWrapper<T> value) {
  if (value.t_) {
    return serializer << true << value.t_;
  } else {
    return serializer << false;
  }
}

Serializer& operator<<(Serializer& serializer, const std::string& value);

// Deserializes values from a byte vector.
class Deserializer {
 public:
  Deserializer(std::vector<uint8_t> serial_message);
  ~Deserializer();

  // Determines whether this |Deserializer| has been successful so far.
  bool healthy() { return healthy_; }

  // Marks the deserializer unhealthy.
  void MarkUnhealthy() { healthy_ = false; }

  // Determines whether this |Deserializer| has successfully consumed the entire
  // serial message.
  bool complete() {
    return healthy_ && bytes_consumed_ == serial_message_.size();
  }

  // Consumes |count| bytes from the serial message and copies them to |dest|
  // if at least |count| bytes remain in the serial message. If less than
  // |count| bytes remain in the the serial message, this method returns false
  // and |healthy| returns false thereafter.
  bool GetBytes(size_t count, void* dest);

  // Consumes |count| bytes from the serial message and returns a pointer to
  // them if at least |count| bytes remain in the serial message. If less than
  // |count| bytes remain in the the serial message, this method returns nullptr
  // and |healthy| returns false thereafter.
  const uint8_t* Bytes(size_t count);

  Deserializer& operator>>(bool& value);
  Deserializer& operator>>(uint8_t& value);
  Deserializer& operator>>(uint16_t& value);
  Deserializer& operator>>(uint32_t& value);
  Deserializer& operator>>(uint64_t& value);
  Deserializer& operator>>(int8_t& value);
  Deserializer& operator>>(int16_t& value);
  Deserializer& operator>>(int32_t& value);
  Deserializer& operator>>(int64_t& value);

 private:
  bool healthy_ = true;
  std::vector<uint8_t> serial_message_;
  size_t bytes_consumed_ = 0;
};

template <typename T>
Deserializer& operator>>(Deserializer& deserializer,
                         SerializationOptionalWrapper<T> value) {
  bool present;
  deserializer >> present;

  if (present) {
    deserializer >> value.t_;
  } else {
    value.t_ = nullptr;
  }

  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer, std::string& value);

}  // namespace media
