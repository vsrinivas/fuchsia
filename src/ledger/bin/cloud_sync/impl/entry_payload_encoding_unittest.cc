// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/entry_payload_encoding.h"

#include "gmock/gmock.h"
#include "src/ledger/bin/cloud_sync/impl/entry_payload_generated.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace cloud_sync {
namespace {

using ::storage::fake::FakeObjectIdentifierFactory;

using EntryPayloadEncodingTest = ::testing::TestWithParam<storage::Entry>;

TEST_P(EntryPayloadEncodingTest, EncodeDecode) {
  const storage::Entry entry = GetParam();
  FakeObjectIdentifierFactory factory;
  std::string payload = EncodeEntryPayload(entry, &factory);
  storage::Entry output;
  ASSERT_TRUE(DecodeEntryPayload(entry.entry_id, payload, &factory, &output));
  EXPECT_EQ(entry, output);
}

INSTANTIATE_TEST_SUITE_P(
    EntryPayloadEncodingTest, EntryPayloadEncodingTest,
    ::testing::Values(
        storage::Entry{"entry_name",
                       storage::ObjectIdentifier(12, storage::ObjectDigest("bananas"), nullptr),
                       storage::KeyPriority::EAGER, "entry_id"},
        storage::Entry{"lazy_entry",
                       storage::ObjectIdentifier(0, storage::ObjectDigest("apple"), nullptr),
                       storage::KeyPriority::LAZY, "entry_id2"}));

TEST(EntryPayloadEncodingTest, ManuallyBuilt) {
  FakeObjectIdentifierFactory factory;

  flatbuffers::FlatBufferBuilder builder;
  storage::ObjectIdentifier object_identifier =
      storage::ObjectIdentifier(12, storage::ObjectDigest("bananas"), nullptr);
  auto entry_name = convert::ToFlatBufferVector(&builder, "entry_name");
  auto object_identifier_off = convert::ToFlatBufferVector(
      &builder, factory.ObjectIdentifierToStorageBytes(object_identifier));
  EntryPayloadBuilder entry_builder(builder);
  entry_builder.add_entry_name(entry_name);
  entry_builder.add_object_identifier(object_identifier_off);
  entry_builder.add_priority(KeyPriority_EAGER);
  builder.Finish(entry_builder.Finish());

  storage::Entry entry;
  ASSERT_TRUE(DecodeEntryPayload("some_id", builder, &factory, &entry));
  EXPECT_EQ(entry, (storage::Entry{"entry_name", object_identifier, storage::KeyPriority::EAGER,
                                   "some_id"}));
}

TEST(EntryPayloadEncodingTest, NoName) {
  FakeObjectIdentifierFactory factory;

  flatbuffers::FlatBufferBuilder builder;
  auto object_identifier = convert::ToFlatBufferVector(
      &builder, factory.ObjectIdentifierToStorageBytes(
                    storage::ObjectIdentifier(12, storage::ObjectDigest("bananas"), nullptr)));
  EntryPayloadBuilder entry_builder(builder);
  entry_builder.add_object_identifier(object_identifier);
  entry_builder.add_priority(KeyPriority_EAGER);
  builder.Finish(entry_builder.Finish());

  storage::Entry entry;
  ASSERT_FALSE(DecodeEntryPayload("some_id", builder, &factory, &entry));
}

TEST(EntryPayloadEncodingTest, NoObjectIdentifier) {
  FakeObjectIdentifierFactory factory;

  flatbuffers::FlatBufferBuilder builder;
  auto entry_name = convert::ToFlatBufferVector(&builder, "entry_name");
  EntryPayloadBuilder entry_builder(builder);
  entry_builder.add_entry_name(entry_name);
  entry_builder.add_priority(KeyPriority_EAGER);
  builder.Finish(entry_builder.Finish());

  storage::Entry entry;
  ASSERT_FALSE(DecodeEntryPayload("some_id", builder, &factory, &entry));
}

TEST(EntryPayloadEncodingTest, InvalidObjectIdentifier) {
  FakeObjectIdentifierFactory factory;

  flatbuffers::FlatBufferBuilder builder;
  auto entry_name = convert::ToFlatBufferVector(&builder, "entry_name");
  auto object_identifier = convert::ToFlatBufferVector(&builder, "fgjdhjfgdjkh");
  EntryPayloadBuilder entry_builder(builder);
  entry_builder.add_entry_name(entry_name);
  entry_builder.add_object_identifier(object_identifier);
  entry_builder.add_priority(KeyPriority_EAGER);
  builder.Finish(entry_builder.Finish());

  storage::Entry entry;
  ASSERT_FALSE(DecodeEntryPayload("some_id", builder, &factory, &entry));
}

}  // namespace
}  // namespace cloud_sync
