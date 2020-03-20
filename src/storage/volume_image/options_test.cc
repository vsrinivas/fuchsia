// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/options.h"

#include <gtest/gtest.h>

namespace storage::volume_image {
namespace {

TEST(CompressionSchemaTest, EnumAsStringIsOk) {
  EXPECT_EQ("COMPRESSION_SCHEMA_NONE", EnumAsString(CompressionSchema::kNone));
  EXPECT_EQ("COMPRESSION_SCHEMA_LZ4", EnumAsString(CompressionSchema::kLz4));
}

TEST(CompressionSchemaTest, StringAsEnumWithValidStringIsOk) {
  {
    auto schema_result = StringAsEnum<CompressionSchema>("COMPRESSION_SCHEMA_NONE");
    ASSERT_TRUE(schema_result.is_ok()) << schema_result.error();
    EXPECT_EQ(CompressionSchema::kNone, schema_result.value());
  }
  {
    auto schema_result = StringAsEnum<CompressionSchema>("COMPRESSION_SCHEMA_LZ4");
    ASSERT_TRUE(schema_result.is_ok()) << schema_result.error();
    EXPECT_EQ(CompressionSchema::kLz4, schema_result.value());
  }
}

TEST(CompressionSchemaTest, StringAsEnumWithInvalidStringIsError) {
  auto schema_result = StringAsEnum<CompressionSchema>("COMPRESSION_SCHEMA_BAD_OR_UNKNOWN");
  ASSERT_TRUE(schema_result.is_error()) << schema_result.error();
}

TEST(EncryptionTypeTest, EnumAsStringIsOk) {
  EXPECT_EQ("ENCRYPTION_TYPE_NONE", EnumAsString(EncryptionType::kNone));
  EXPECT_EQ("ENCRYPTION_TYPE_ZXCRYPT", EnumAsString(EncryptionType::kZxcrypt));
}

TEST(EncryptionTypeTest, StringAsEnumWithValidStringIsOk) {
  {
    auto encryption_type_result = StringAsEnum<EncryptionType>("ENCRYPTION_TYPE_NONE");
    ASSERT_TRUE(encryption_type_result.is_ok()) << encryption_type_result.error();
    EXPECT_EQ(EncryptionType::kNone, encryption_type_result.value());
  }
  {
    auto encryption_type_result = StringAsEnum<EncryptionType>("ENCRYPTION_TYPE_ZXCRYPT");
    ASSERT_TRUE(encryption_type_result.is_ok()) << encryption_type_result.error();
    EXPECT_EQ(EncryptionType::kZxcrypt, encryption_type_result.value());
  }
}

TEST(EncryptionTypeTest, StringAsEnumWithInvalidStringIsError) {
  auto encryption_type_result = StringAsEnum<EncryptionType>("ENCRYPTION_TYPE_BAD_OR_UNKNOWN");
  ASSERT_TRUE(encryption_type_result.is_error()) << encryption_type_result.error();
}

TEST(OptionTest, EnumAsStringIsOk) {
  EXPECT_EQ("OPTION_NONE", EnumAsString(Option::kNone));
  EXPECT_EQ("OPTION_EMPTY", EnumAsString(Option::kEmpty));
}

TEST(OptionTest, StringAsEnumWithValidStringIsOk) {
  {
    auto option_result = StringAsEnum<Option>("OPTION_NONE");
    ASSERT_TRUE(option_result.is_ok()) << option_result.error();
    EXPECT_EQ(Option::kNone, option_result.value());
  }
  {
    auto option_result = StringAsEnum<Option>("OPTION_EMPTY");
    ASSERT_TRUE(option_result.is_ok()) << option_result.error();
    EXPECT_EQ(Option::kEmpty, option_result.value());
  }
}

TEST(OptionTest, StringAsEnumWithInvalidStringIsError) {
  auto option_result = StringAsEnum<Option>("OPTION_BAD_OR_UNKNOWN");
  ASSERT_TRUE(option_result.is_error()) << option_result.error();
}

}  // namespace
}  // namespace storage::volume_image
