// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_object.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {
namespace fake {
namespace {

TEST(FakeObjectTest, FakePiece) {
  const std::string content = "some content";
  const ObjectIdentifier identifier(1u, 2u, ObjectDigest("some digest"));
  const FakePiece piece(identifier, content);

  EXPECT_EQ(piece.GetData(), content);
  EXPECT_EQ(piece.GetIdentifier(), identifier);
  ObjectReferencesAndPriority references;
  EXPECT_EQ(piece.AppendReferences(&references), Status::OK);
  EXPECT_TRUE(references.empty());
}

TEST(FakeObjectTest, FakeObject) {
  const std::string content = "some content";
  const ObjectIdentifier identifier(1u, 2u, ObjectDigest("some digest"));
  const FakeObject object(std::make_unique<FakePiece>(identifier, content));

  fxl::StringView data;
  ASSERT_EQ(object.GetData(&data), Status::OK);
  EXPECT_EQ(data, content);
  EXPECT_EQ(object.GetIdentifier(), identifier);
  ObjectReferencesAndPriority references;
  EXPECT_EQ(object.AppendReferences(&references), Status::OK);
  EXPECT_TRUE(references.empty());
}

TEST(FakeObjectTest, FakeTokenChecker) {
  const ObjectIdentifier identifier(1u, 2u, ObjectDigest("some digest"));
  auto token = std::make_unique<FakePieceToken>(identifier);
  EXPECT_EQ(token->GetIdentifier(), identifier);

  const FakeTokenChecker checker = token->GetChecker();
  EXPECT_TRUE(checker);
  token.reset();
  EXPECT_FALSE(checker);
}

TEST(FakeObjectTest, TracksToken) {
  const ObjectIdentifier identifier(1u, 2u, ObjectDigest("some digest"));
  auto fake_token = std::make_unique<FakePieceToken>(identifier);
  const FakeTokenChecker checker = fake_token->GetChecker();

  // Cast fake_token to a regular token to check TracksToken.
  std::unique_ptr<const PieceToken> token(fake_token.release());

  EXPECT_TRUE(checker.TracksToken(token));

  std::unique_ptr<const PieceToken> another_token = std::make_unique<FakePieceToken>(identifier);
  EXPECT_FALSE(checker.TracksToken(another_token));
  EXPECT_FALSE(checker.TracksToken(nullptr));
}

}  // namespace
}  // namespace fake
}  // namespace storage
