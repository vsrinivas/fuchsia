// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

#include "gtest/gtest.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/storage/fake/fake_object.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace encryption {
namespace {

class EncryptionServiceTest : public ::test::TestWithMessageLoop {
 public:
  EncryptionServiceTest() : encryption_service_(message_loop_.task_runner()) {}

 protected:
  void EncryptCommit(convert::ExtendedStringView commit_storage,
                     Status* status,
                     std::string* result) {
    encryption_service_.EncryptCommit(
        commit_storage, callback::Capture(MakeQuitTask(), status, result));
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  void DecryptCommit(convert::ExtendedStringView encrypted_commit_storage,
                     Status* status,
                     std::string* result) {
    encryption_service_.DecryptCommit(
        encrypted_commit_storage,
        callback::Capture(MakeQuitTask(), status, result));
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  void GetObjectName(storage::ObjectIdentifier object_identifier,
                     Status* status,
                     std::string* result) {
    encryption_service_.GetObjectName(
        std::move(object_identifier),
        callback::Capture(MakeQuitTask(), status, result));
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  void EncryptObject(std::unique_ptr<const storage::Object> object,
                     Status* status,
                     std::string* result) {
    encryption_service_.EncryptObject(
        std::move(object), callback::Capture(MakeQuitTask(), status, result));
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  void DecryptObject(storage::ObjectIdentifier object_identifier,
                     std::string encrypted_data,
                     Status* status,
                     std::string* result) {
    encryption_service_.DecryptObject(
        std::move(object_identifier), std::move(encrypted_data),
        callback::Capture(MakeQuitTask(), status, result));
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  EncryptionServiceImpl encryption_service_;
};

TEST_F(EncryptionServiceTest, EncryptDecryptCommit) {
  fxl::StringView contents[] = {
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

  Status status;
  std::string encrypted_bytes;
  EncryptObject(std::make_unique<storage::fake::FakeObject>(
                    identifier.object_digest, content),
                &status, &encrypted_bytes);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(encrypted_bytes.empty());

  std::string decrypted_bytes;
  DecryptObject(identifier, encrypted_bytes, &status, &decrypted_bytes);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(content, decrypted_bytes);
}

}  // namespace
}  // namespace encryption
