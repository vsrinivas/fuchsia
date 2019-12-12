// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/encryption_service_impl.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace encryption {
namespace {

class EncryptionServiceTest : public ledger::TestWithEnvironment {
 public:
  EncryptionServiceTest() : encryption_service_(&environment_, "namespace_id") {}

 protected:
  void EncryptCommit(std::string commit_storage, Status* status, std::string* result) {
    bool called;
    encryption_service_.EncryptCommit(
        commit_storage, ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void DecryptCommit(convert::ExtendedStringView encrypted_commit_storage, Status* status,
                     std::string* result) {
    bool called;
    encryption_service_.DecryptCommit(
        encrypted_commit_storage, ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void EncryptEntryPayload(std::string entry_storage, Status* status, std::string* result) {
    bool called;
    encryption_service_.EncryptEntryPayload(
        entry_storage, ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void DecryptEntryPayload(std::string encrypted_entry_storage, Status* status,
                           std::string* result) {
    bool called;
    encryption_service_.DecryptEntryPayload(
        encrypted_entry_storage, ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void GetObjectName(storage::ObjectIdentifier object_identifier, Status* status,
                     std::string* result) {
    bool called;
    encryption_service_.GetObjectName(
        std::move(object_identifier),
        ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void GetPageId(std::string page_name, Status* status, std::string* result) {
    bool called;
    encryption_service_.GetPageId(std::move(page_name),
                                  ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void EncryptObject(storage::ObjectIdentifier object_identifier, absl::string_view content,
                     Status* status, std::string* result) {
    bool called;
    encryption_service_.EncryptObject(
        std::move(object_identifier), content,
        ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void DecryptObject(storage::ObjectIdentifier object_identifier, std::string encrypted_data,
                     Status* status, std::string* result) {
    bool called;
    encryption_service_.DecryptObject(
        std::move(object_identifier), std::move(encrypted_data),
        ledger::Capture(ledger::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void ApplyChunkingPermutation(uint64_t chunk_window_hash, Status* status, uint64_t* result) {
    bool called;
    fit::function<uint64_t(uint64_t)> permutation;
    encryption_service_.GetChunkingPermutation(
        ledger::Capture(ledger::SetWhenCalled(&called), status, &permutation));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    *result = permutation(chunk_window_hash);
  }

  std::string GetEntryId() { return encryption_service_.GetEntryId(); }
  std::string GetEntryIdForMerge(absl::string_view entry_name, storage::CommitId left_parent_id,
                                 storage::CommitId right_parent_id,
                                 absl::string_view operation_list) {
    return encryption_service_.GetEntryIdForMerge(entry_name, left_parent_id, right_parent_id,
                                                  operation_list);
  }

  std::string EncodeCommitId(std::string commit_id) {
    return encryption_service_.EncodeCommitId(std::move(commit_id));
  }

  bool IsSameVersion(convert::ExtendedStringView remote_commit_id) {
    return encryption_service_.IsSameVersion(remote_commit_id);
  }

  EncryptionServiceImpl encryption_service_;
};

TEST_F(EncryptionServiceTest, EncryptDecryptCommit) {
  std::string contents[] = {
      "",
      "Hello",
      "0123456789012345678901234567890123456789012345678901234567890123456789",

  };

  for (const auto& content : contents) {
    Status status;
    std::string value;
    EncryptCommit(content, &status, &value);
    ASSERT_EQ(status, Status::OK);
    DecryptCommit(value, &status, &value);
    ASSERT_EQ(status, Status::OK);
    EXPECT_EQ(value, content);
  }
}

TEST_F(EncryptionServiceTest, EncryptDecryptEntryPayload) {
  std::string contents[] = {
      "",
      "SomeEntry",
      "0123456789012345678901234567890123456789012345678901234567890123456789",
  };

  for (const auto& content : contents) {
    Status status;
    std::string value;
    EncryptEntryPayload(content, &status, &value);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(value, content);
    DecryptEntryPayload(value, &status, &value);
    ASSERT_EQ(status, Status::OK);
    EXPECT_EQ(value, content);
  }
}

TEST_F(EncryptionServiceTest, GetName) {
  storage::ObjectIdentifier identifier(42u, storage::ObjectDigest(std::string(33u, '\0')), nullptr);
  Status status;
  std::string name;
  GetObjectName(identifier, &status, &name);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(name.empty());
}

TEST_F(EncryptionServiceTest, GetPageId) {
  std::string page_name = "Jimmy";
  Status status;
  std::string id;
  GetPageId(page_name, &status, &id);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(id.empty());
  EXPECT_NE(id, page_name);
}

TEST_F(EncryptionServiceTest, EncryptDecryptObject) {
  storage::ObjectIdentifier identifier(42u, storage::ObjectDigest(std::string(33u, '\0')), nullptr);
  std::string content(256u, '\0');
  std::unique_ptr<storage::Object> object =
      std::make_unique<storage::fake::FakeObject>(identifier, content);
  absl::string_view content_data;
  ASSERT_EQ(object->GetData(&content_data), ledger::Status::OK);

  Status status;
  std::string encrypted_bytes;
  EncryptObject(object->GetIdentifier(), content_data, &status, &encrypted_bytes);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(encrypted_bytes.empty());

  std::string decrypted_bytes;
  DecryptObject(identifier, encrypted_bytes, &status, &decrypted_bytes);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(decrypted_bytes, content);
}

TEST_F(EncryptionServiceTest, GetApplyChunkingPermutation) {
  uint64_t chunk_window_hash, result;
  Status status;
  auto bit_generator = environment_.random()->NewBitGenerator<uint64_t>();
  chunk_window_hash =
      std::uniform_int_distribution(0ul, std::numeric_limits<uint64_t>::max())(bit_generator);
  ApplyChunkingPermutation(chunk_window_hash, &status, &result);
  EXPECT_EQ(status, Status::OK);
  EXPECT_NE(chunk_window_hash, result);
  // Since we're using xor, applying the same permutation two times should yield
  // the initial input;
  ApplyChunkingPermutation(result, &status, &result);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(result, chunk_window_hash);
}

TEST_F(EncryptionServiceTest, GetEntryIdMergeCommit) {
  storage::CommitId parent_id1 = "commit1";
  storage::CommitId parent_id2 = "commit2";
  std::string entry_name = "Name";
  std::string operation_list = "AADD";

  std::string entry_id = GetEntryIdForMerge(entry_name, parent_id1, parent_id2, operation_list);
  // For merge commits, calling this method with the same parameters must result in the same entry
  // id.
  std::string entry_id0 = GetEntryIdForMerge(entry_name, parent_id1, parent_id2, operation_list);
  EXPECT_EQ(entry_id, entry_id0);

  // Changing any of the parameters must result in different entry id.
  EXPECT_NE(entry_id, GetEntryIdForMerge(entry_name, parent_id1, parent_id2, "AD"));
  EXPECT_NE(entry_id, GetEntryIdForMerge(entry_name, parent_id1, "commit3", operation_list));
  EXPECT_NE(entry_id, GetEntryIdForMerge("Surname", parent_id1, parent_id2, operation_list));
  // Changing the order of the parents must result in different entry ids.
  EXPECT_NE(entry_id, GetEntryIdForMerge(entry_name, parent_id2, parent_id1, operation_list));
}

TEST_F(EncryptionServiceTest, GetEntryIdNonMergeCommit) { EXPECT_NE(GetEntryId(), GetEntryId()); }

TEST_F(EncryptionServiceTest, EncodeVerifyCommitId) {
  std::string encoded_id1 = EncodeCommitId("commit_id1");
  EXPECT_TRUE(IsSameVersion(encoded_id1));

  std::string encoded_id2 = EncodeCommitId("commit_id2");
  EXPECT_TRUE(IsSameVersion(encoded_id2));

  EXPECT_NE(encoded_id1, encoded_id2);
}

}  // namespace
}  // namespace encryption
