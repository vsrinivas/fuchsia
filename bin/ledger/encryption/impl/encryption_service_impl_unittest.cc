// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/storage/fake/fake_object.h"

namespace encryption {
namespace {

class EncryptionServiceTest : public gtest::TestLoopFixture {
 public:
  EncryptionServiceTest() : encryption_service_(dispatcher(), "namespace_id") {}

 protected:
  void EncryptCommit(std::string commit_storage, Status* status,
                     std::string* result) {
    bool called;
    encryption_service_.EncryptCommit(
        commit_storage,
        callback::Capture(callback::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void DecryptCommit(convert::ExtendedStringView encrypted_commit_storage,
                     Status* status, std::string* result) {
    bool called;
    encryption_service_.DecryptCommit(
        encrypted_commit_storage,
        callback::Capture(callback::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void GetObjectName(storage::ObjectIdentifier object_identifier,
                     Status* status, std::string* result) {
    bool called;
    encryption_service_.GetObjectName(
        std::move(object_identifier),
        callback::Capture(callback::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void EncryptObject(storage::ObjectIdentifier object_identifier,
                     fsl::SizedVmo content, Status* status,
                     std::string* result) {
    bool called;
    encryption_service_.EncryptObject(
        std::move(object_identifier), std::move(content),
        callback::Capture(callback::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  void DecryptObject(storage::ObjectIdentifier object_identifier,
                     std::string encrypted_data, Status* status,
                     std::string* result) {
    bool called;
    encryption_service_.DecryptObject(
        std::move(object_identifier), std::move(encrypted_data),
        callback::Capture(callback::SetWhenCalled(&called), status, result));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
  }

  EncryptionServiceImpl encryption_service_;
};

TEST_F(EncryptionServiceTest, EncryptDecryptCommit) {
  std::string contents[] = {
      "",
      "Hello",
      "0123456789012345678901234567890123456789012345678901234567890123456789",

  };

  for (auto content : contents) {
    Status status;
    std::string value;
    EncryptCommit(content, &status, &value);
    ASSERT_EQ(Status::OK, status);
    DecryptCommit(value, &status, &value);
    ASSERT_EQ(Status::OK, status);
    EXPECT_EQ(content, value);
  }
}

TEST_F(EncryptionServiceTest, GetName) {
  storage::ObjectIdentifier identifier{42u, 42u, std::string(33u, '\0')};
  Status status;
  std::string name;
  GetObjectName(identifier, &status, &name);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(name.empty());
}

TEST_F(EncryptionServiceTest, EncryptDecryptObject) {
  storage::ObjectIdentifier identifier{42u, 42u, std::string(33u, '\0')};
  std::string content(256u, '\0');
  auto object =
      std::make_unique<storage::fake::FakeObject>(identifier, content);
  fsl::SizedVmo content_vmo;
  ASSERT_EQ(storage::Status::OK, object->GetVmo(&content_vmo));

  Status status;
  std::string encrypted_bytes;
  EncryptObject(object->GetIdentifier(), std::move(content_vmo), &status,
                &encrypted_bytes);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(encrypted_bytes.empty());

  std::string decrypted_bytes;
  DecryptObject(identifier, encrypted_bytes, &status, &decrypted_bytes);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(content, decrypted_bytes);
}

}  // namespace
}  // namespace encryption
