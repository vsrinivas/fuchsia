// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "apps/modular/services/document_store/document.fidl.h"

// This file contains utility functions which convert values to and from the
// format which is used to store these values on the ledger.
// See SerializeValue and DeserializeValue.

namespace document_store {
namespace internal {

enum class ValuePrefix : uint8_t {
  STRING = 's',
  IRI = 'r',
  INT = 'i',
  FLOAT = 'f',
  BINARY = 'b',
  BOOL = 't',
  EMPTY = 'e',
};

// Serialize an integer value for storage on the ledger.
void SerializeInt(const int64_t value, fidl::Array<uint8_t>* serialized) {
  *serialized = fidl::Array<uint8_t>::New(1 + sizeof(int64_t));

  uint8_t* raw = serialized->data();
  raw[0] = static_cast<uint8_t>(ValuePrefix::INT);

  *reinterpret_cast<int64_t*>(raw + 1) = value;
}

// Serialize a floating point value for storage on the ledger.
void SerializeFloat(const double value, fidl::Array<uint8_t>* serialized) {
  *serialized = fidl::Array<uint8_t>::New(1 + sizeof(double));

  uint8_t* raw = serialized->data();
  raw[0] = static_cast<uint8_t>(ValuePrefix::FLOAT);

  *reinterpret_cast<double*>(raw + 1) = value;
}

// Serialize a string value for storage on the ledger.
void SerializeStringOrIri(const fidl::String& value, const ValuePrefix prefix,
                          fidl::Array<uint8_t>* serialized) {
  *serialized = fidl::Array<uint8_t>::New(1 + value.size());

  uint8_t* raw = serialized->data();
  raw[0] = static_cast<uint8_t>(prefix);

  const char* value_raw = value.data();
  std::copy(value_raw, value_raw + value.size(), &raw[1]);
}

void SerializeBinary(const fidl::Array<uint8_t>& value,
                     fidl::Array<uint8_t>* serialized) {
  *serialized = fidl::Array<uint8_t>::New(1 + value.size());

  uint8_t* raw = serialized->data();
  raw[0] = static_cast<uint8_t>(ValuePrefix::BINARY);

  const uint8_t* value_raw = value.data();
  std::copy(value_raw, value_raw + value.size(), &raw[1]);
}

void SerializeEmpty(fidl::Array<uint8_t>* serialized) {
  *serialized = fidl::Array<uint8_t>::New(1);
  serialized->data()[0] = static_cast<uint8_t>(ValuePrefix::EMPTY);
}

void SerializeBool(const bool value, fidl::Array<uint8_t>* serialized) {
  *serialized = fidl::Array<uint8_t>::New(2);
  uint8_t* raw = serialized->data();

  raw[0] = static_cast<uint8_t>(ValuePrefix::EMPTY);

  *reinterpret_cast<bool*>(raw + 1) = value;
}

// Serialize a value for storage on the ledger.
void SerializeValue(const ValuePtr& value, fidl::Array<uint8_t>* serialized) {
  fidl::String string;
  switch (value->which()) {
    case Value::Tag::STRING_VALUE:
      SerializeStringOrIri(value->get_string_value(), ValuePrefix::STRING,
                           serialized);
      return;
    case Value::Tag::IRI:
      SerializeStringOrIri(value->get_iri(), ValuePrefix::IRI, serialized);
      return;
    case Value::Tag::INT_VALUE:
      SerializeInt(value->get_int_value(), serialized);
      return;
    case Value::Tag::FLOAT_VALUE:
      SerializeFloat(value->get_float_value(), serialized);
      return;
    case Value::Tag::BINARY:
      SerializeBinary(value->get_binary(), serialized);
      return;
    case Value::Tag::BOOL_VALUE:
      SerializeBool(value->get_bool_value(), serialized);
      return;
    case Value::Tag::EMPTY:
      SerializeEmpty(serialized);
      return;
    case Value::Tag::__UNKNOWN__:
      FTL_LOG(FATAL) << "Unsupported data type!";
  };
}

// Deserialize a string stored on the ledger.
void DeserializeString(const fidl::Array<uint8_t>& serialized,
                       ValuePtr* value) {
  fidl::String string(reinterpret_cast<const char*>(serialized.data() + 1),
                      serialized.size() - 1);
  (*value)->set_string_value(std::move(string));
}

// Deserialize an iri stored on the ledger.
void DeserializeIri(const fidl::Array<uint8_t>& serialized, ValuePtr* value) {
  fidl::String string(reinterpret_cast<const char*>(serialized.data() + 1),
                      serialized.size() - 1);
  (*value)->set_iri(std::move(string));
}

// Deserialize a binary value stored on the ledger.
void DeserializeBinary(const fidl::Array<uint8_t>& serialized,
                       ValuePtr* value) {
  fidl::Array<uint8_t> binary =
      fidl::Array<uint8_t>::New(serialized.size() - 1);
  auto binary_raw = binary.data();
  std::copy(serialized.data() + 1, serialized.data() + serialized.size() + 1,
            binary_raw);

  (*value)->set_binary(std::move(binary));
}

// Deserialize an integer value on the ledger.
bool DeserializeInt(const fidl::Array<uint8_t>& serialized, ValuePtr* value) {
  if (serialized.size() != sizeof(int64_t) + 1) {
    return false;
  }
  (*value)->set_int_value(
      *reinterpret_cast<const int64_t*>(serialized.data() + 1));
  return true;
}

// Deserialize a floating point value on the ledger.
bool DeserializeFloat(const fidl::Array<uint8_t>& serialized, ValuePtr* value) {
  if (serialized.size() != sizeof(double) + 1) {
    return false;
  }
  (*value)->set_float_value(
      *reinterpret_cast<const double*>(serialized.data() + 1));
  return true;
}

bool DeserializeBool(const fidl::Array<uint8_t>& serialized, ValuePtr* value) {
  if (serialized.size() != 2) {
    return false;
  }

  (*value)->set_bool_value(
      *reinterpret_cast<const bool*>(serialized.data() + 1));

  return true;
}

// Deserialize a value stored on the ledger.
bool DeserializeValue(const fidl::Array<uint8_t>& serialized, ValuePtr* value) {
  *value = Value::New();

  switch (static_cast<ValuePrefix>(serialized[0])) {
    case ValuePrefix::STRING:
      DeserializeString(serialized, value);
      return true;
    case ValuePrefix::IRI:
      DeserializeIri(serialized, value);
      return true;
    case ValuePrefix::BINARY:
      DeserializeBinary(serialized, value);
      return true;
    case ValuePrefix::INT:
      return DeserializeInt(serialized, value);
    case ValuePrefix::FLOAT:
      return DeserializeFloat(serialized, value);
    case ValuePrefix::BOOL:
      return DeserializeBool(serialized, value);
    case ValuePrefix::EMPTY:
      (*value)->set_empty(true);
      return true;
  };
  FTL_LOG(ERROR) << "Unrecognized data type.";
  return false;
}

}  // namespace internal
}  // namespace document_store
