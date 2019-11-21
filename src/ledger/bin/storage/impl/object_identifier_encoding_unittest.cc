// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"

namespace storage {
namespace {

using ObjectIdentifierEncodingTest = ::testing::TestWithParam<ObjectIdentifier>;

TEST_P(ObjectIdentifierEncodingTest, EncodeDecode) {
  const ObjectIdentifier object_identifier = GetParam();
  fake::FakeObjectIdentifierFactory factory;
  std::string data = EncodeObjectIdentifier(object_identifier);
  ObjectIdentifier output;
  ASSERT_TRUE(DecodeObjectIdentifier(data, &factory, &output));
  EXPECT_EQ(object_identifier, output);
}

INSTANTIATE_TEST_SUITE_P(ObjectIdentifierEncodingTest, ObjectIdentifierEncodingTest,
                         ::testing::Values(ObjectIdentifier(0, ObjectDigest("\0pen"), nullptr),
                                           ObjectIdentifier(78, ObjectDigest("pineapple"),
                                                            nullptr)));

TEST(ObjectIdentifierEncodingTest, ManuallyBuilt) {
  flatbuffers::FlatBufferBuilder builder;
  auto object_digest = convert::ToFlatBufferVector(&builder, "apples");
  ObjectIdentifierStorageBuilder object_identifier_builder(builder);
  object_identifier_builder.add_object_digest(object_digest);
  object_identifier_builder.add_key_index(12);
  builder.Finish(object_identifier_builder.Finish());

  fake::FakeObjectIdentifierFactory factory;
  ObjectIdentifier object_identifier;
  ASSERT_TRUE(DecodeObjectIdentifier(convert::ToStringView(builder), &factory, &object_identifier));
  EXPECT_EQ(object_identifier, factory.MakeObjectIdentifier(12, ObjectDigest("apples")));
}

TEST(ObjectIdentifierEncodingTest, MissingObjectDigest) {
  flatbuffers::FlatBufferBuilder builder;
  ObjectIdentifierStorageBuilder object_identifier_builder(builder);
  object_identifier_builder.add_key_index(12);
  builder.Finish(object_identifier_builder.Finish());

  fake::FakeObjectIdentifierFactory factory;
  ObjectIdentifier object_identifier;
  ASSERT_FALSE(
      DecodeObjectIdentifier(convert::ToStringView(builder), &factory, &object_identifier));
}

using ObjectIdentifierDigestPrefixedEncodingTest = ledger::TestWithEnvironment;

TEST_F(ObjectIdentifierDigestPrefixedEncodingTest, EncodeDecode) {
  fake::FakeObjectIdentifierFactory factory;
  const ObjectIdentifier object_identifier =
      RandomObjectIdentifier(environment_.random(), &factory);
  std::string data = EncodeDigestPrefixedObjectIdentifier(object_identifier);
  ObjectIdentifier output;
  ASSERT_TRUE(DecodeDigestPrefixedObjectIdentifier(data, &factory, &output));
  EXPECT_EQ(object_identifier, output);
}

TEST_F(ObjectIdentifierDigestPrefixedEncodingTest, InvalidInput) {
  fake::FakeObjectIdentifierFactory factory;
  ObjectIdentifier output;
  // Input too short.
  EXPECT_FALSE(DecodeDigestPrefixedObjectIdentifier("foo", &factory, &output));
  // Input too long.
  EXPECT_FALSE(DecodeDigestPrefixedObjectIdentifier("0123456789ABCDEF0123456789ABCDEF012345",
                                                    &factory, &output));
  // Invalid object digest.
  EXPECT_FALSE(
      DecodeDigestPrefixedObjectIdentifier("\xf"
                                           "123456789ABCDEF0123456789ABCDEF01234",
                                           &factory, &output));
}

}  // namespace
}  // namespace storage
