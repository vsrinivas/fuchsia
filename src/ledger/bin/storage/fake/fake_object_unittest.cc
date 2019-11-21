// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_object.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace fake {
namespace {

TEST(FakeObjectTest, FakePiece) {
  FakeObjectIdentifierFactory factory;
  const std::string content = "some content";
  const ObjectIdentifier identifier = factory.MakeObjectIdentifier(1u, ObjectDigest("some digest"));
  const FakePiece piece(identifier, content);

  EXPECT_EQ(piece.GetData(), content);
  EXPECT_EQ(piece.GetIdentifier(), identifier);
  ObjectReferencesAndPriority references;
  EXPECT_EQ(piece.AppendReferences(&references), Status::OK);
  EXPECT_TRUE(references.empty());
}

TEST(FakeObjectTest, FakeObject) {
  FakeObjectIdentifierFactory factory;
  const std::string content = "some content";
  const ObjectIdentifier identifier = factory.MakeObjectIdentifier(1u, ObjectDigest("some digest"));
  const FakeObject object(std::make_unique<FakePiece>(identifier, content));

  absl::string_view data;
  ASSERT_EQ(object.GetData(&data), Status::OK);
  EXPECT_EQ(data, content);
  EXPECT_EQ(object.GetIdentifier(), identifier);
  ObjectReferencesAndPriority references;
  EXPECT_EQ(object.AppendReferences(&references), Status::OK);
  EXPECT_TRUE(references.empty());
}

}  // namespace
}  // namespace fake
}  // namespace storage
