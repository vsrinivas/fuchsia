// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/types.h"

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
                         ::testing::Values(ObjectIdentifier(0, 0, ObjectDigest("\0pen"), nullptr),
                                           ObjectIdentifier(78, 235, ObjectDigest("pineapple"),
                                                            nullptr)));

TEST(ObjectIdentifierEncodingTest, ManuallyBuilt) {
  flatbuffers::FlatBufferBuilder builder;
  auto object_digest = convert::ToFlatBufferVector(&builder, "apples");
  ObjectIdentifierStorageBuilder object_identifier_builder(builder);
  object_identifier_builder.add_object_digest(object_digest);
  object_identifier_builder.add_key_index(12);
  object_identifier_builder.add_deletion_scope_id(456);
  builder.Finish(object_identifier_builder.Finish());

  fake::FakeObjectIdentifierFactory factory;
  ObjectIdentifier object_identifier;
  ASSERT_TRUE(DecodeObjectIdentifier(convert::ToStringView(builder), &factory, &object_identifier));
  EXPECT_EQ(object_identifier, factory.MakeObjectIdentifier(12, 456, ObjectDigest("apples")));
}

TEST(ObjectIdentifierEncodingTest, MissingObjectDigest) {
  flatbuffers::FlatBufferBuilder builder;
  ObjectIdentifierStorageBuilder object_identifier_builder(builder);
  object_identifier_builder.add_key_index(12);
  object_identifier_builder.add_deletion_scope_id(456);
  builder.Finish(object_identifier_builder.Finish());

  fake::FakeObjectIdentifierFactory factory;
  ObjectIdentifier object_identifier;
  ASSERT_FALSE(
      DecodeObjectIdentifier(convert::ToStringView(builder), &factory, &object_identifier));
}

}  // namespace
}  // namespace storage
