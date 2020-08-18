// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/options.h"

#include <cstdlib>
#include <string>

namespace storage::volume_image {

template <>
std::string EnumAsString(CompressionSchema schema) {
  switch (schema) {
    case CompressionSchema::kNone:
      return "COMPRESSION_SCHEMA_NONE";
    case CompressionSchema::kLz4:
      return "COMPRESSION_SCHEMA_LZ4";
    default:
      return "COMPRESSION_SCHEMA_UNKNOWN";
  }
}

template <>
std::string EnumAsString(EncryptionType encryption) {
  switch (encryption) {
    case EncryptionType::kNone:
      return "ENCRYPTION_TYPE_NONE";
    case EncryptionType::kZxcrypt:
      return "ENCRYPTION_TYPE_ZXCRYPT";
  }
}

template <>
std::string EnumAsString(Option option) {
  switch (option) {
    case Option::kNone:
      return "OPTION_NONE";
    case Option::kEmpty:
      return "OPTION_EMPTY";
  }
}

template <>
std::string EnumAsString(AddressMapOption option) {
  switch (option) {
    case AddressMapOption::kFill:
      return "ADDRESS_MAP_OPTION_FILL";
    default:
      return "ADDRESS_MAP_OPTION_UNKNOWN";
  }
}

template <>
fit::result<CompressionSchema, std::string> StringAsEnum(std::string_view compression) {
  static const std::unordered_map<std::string_view, CompressionSchema> string_to_compression = {
      {"COMPRESSION_SCHEMA_NONE", CompressionSchema::kNone},
      {"COMPRESSION_SCHEMA_LZ4", CompressionSchema::kLz4}};
  if (string_to_compression.find(compression) == string_to_compression.end()) {
    std::string error = "Unknown compression scheme(";
    error.append(compression).append(").\n");
    return fit::error(std::move(error));
  }

  return fit::ok(string_to_compression.at(compression));
}

template <>
fit::result<EncryptionType, std::string> StringAsEnum(std::string_view encryption) {
  static const std::unordered_map<std::string_view, EncryptionType> string_to_encryption = {
      {"ENCRYPTION_TYPE_NONE", EncryptionType::kNone},
      {"ENCRYPTION_TYPE_ZXCRYPT", EncryptionType::kZxcrypt}};
  if (string_to_encryption.find(encryption) == string_to_encryption.end()) {
    std::string error = "Unknown encryption type(";
    error.append(encryption).append(").\n");
    return fit::error(std::move(error));
  }

  return fit::ok(string_to_encryption.at(encryption));
}

template <>
fit::result<Option, std::string> StringAsEnum(std::string_view option) {
  static const std::unordered_map<std::string_view, Option> string_to_option = {
      {"OPTION_NONE", Option::kNone}, {"OPTION_EMPTY", Option::kEmpty}};
  if (string_to_option.find(option) == string_to_option.end()) {
    std::string error = "Unknown option type(";
    error.append(option).append(").\n");
    return fit::error(std::move(error));
  }

  return fit::ok(string_to_option.at(option));
}

template <>
fit::result<AddressMapOption, std::string> StringAsEnum(std::string_view option) {
  static const std::unordered_map<std::string_view, AddressMapOption> string_to_option = {
      {"ADDRESS_MAP_OPTION_FILL", AddressMapOption::kFill}};
  if (string_to_option.find(option) == string_to_option.end()) {
    return fit::error("Unknown AddressMapOption type(" + std::string(option) + ").\n");
  }

  return fit::ok(string_to_option.at(option));
}

}  // namespace storage::volume_image
