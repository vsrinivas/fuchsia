// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/key_service.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace encryption {
namespace {

class KeyServiceTest : public ledger::TestWithEnvironment {
 public:
  KeyServiceTest() : key_service_(environment_.dispatcher(), "namespace_id") {}

  KeyService key_service_;
};

TEST_F(KeyServiceTest, GetChunkingKey) {
  Status status;
  std::string result;
  bool called;
  key_service_.GetChunkingKey(ledger::Capture(ledger::SetWhenCalled(&called), &status, &result));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(result.empty());
}

TEST_F(KeyServiceTest, GetPageIdKey) {
  Status status;
  std::string result;
  bool called;
  key_service_.GetPageIdKey(ledger::Capture(ledger::SetWhenCalled(&called), &status, &result));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(result.empty());
}

TEST_F(KeyServiceTest, GetEncryptionKey) {
  Status status;
  bool called;
  uint64_t key_index1 = 0u;
  uint64_t key_index2 = kDefaultKeyIndex;

  std::string encryption_key1;
  key_service_.GetEncryptionKey(
      key_index1, ledger::Capture(ledger::SetWhenCalled(&called), &status, &encryption_key1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(encryption_key1.empty());

  std::string encryption_key2;
  key_service_.GetEncryptionKey(
      key_index2, ledger::Capture(ledger::SetWhenCalled(&called), &status, &encryption_key2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(encryption_key2.empty());

  EXPECT_NE(encryption_key1, encryption_key2);
}

TEST_F(KeyServiceTest, GetRemoteObjectIdKey) {
  Status status;
  bool called;
  uint64_t key_index1 = 0u;
  uint64_t key_index2 = kDefaultKeyIndex;

  std::string remote_object_id_key1;
  key_service_.GetRemoteObjectIdKey(
      key_index1, ledger::Capture(ledger::SetWhenCalled(&called), &status, &remote_object_id_key1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(remote_object_id_key1.empty());

  std::string remote_object_id_key2;
  key_service_.GetRemoteObjectIdKey(
      key_index2, ledger::Capture(ledger::SetWhenCalled(&called), &status, &remote_object_id_key2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(remote_object_id_key2.empty());

  EXPECT_NE(remote_object_id_key1, remote_object_id_key2);
}

}  // namespace
}  // namespace encryption
