// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

#include "gtest/gtest.h"
#include "peridot/bin/ledger/callback/capture.h"
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

  EncryptionServiceImpl encryption_service_;
};

TEST_F(EncryptionServiceTest, EncryptionDescription) {
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

}  // namespace
}  // namespace encryption
