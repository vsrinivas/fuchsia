// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth_db_file_impl.h"

#include <stdlib.h>

#include <random>

#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"

namespace auth {
namespace store {

namespace {

constexpr char kGoogleIdp[] = "Google";
constexpr char kTestIdp[] = "Test";

// Generates a idp userid string based on the given |index| and |idp|.
std::string IdpUserIdString(uint32_t index, const std::string& idp) {
  std::string out(15, 0);
  std::snprintf(&out[0], out.size(), "%s_user_%d", idp.c_str(), index);
  return out;
}

// Generates a refresh token string based on the given |index| and |idp|.
std::string RefreshTokenString(uint32_t index, const std::string& idp) {
  std::string out(27, 0);
  std::snprintf(&out[0], out.size(), "%s_rt_%d", idp.c_str(), index);
  return out;
}

// Returns a new CredentialIdentifer using the given |index| and |idp|.
CredentialIdentifier MakeCredentialIdentifier(uint32_t index,
                                              const std::string& idp) {
  return CredentialIdentifier(IdpUserIdString(index, idp), idp);
}

// Returns a new CredentialValue by generating a new refresh token based on the
// given |index| and |idp| values.
CredentialValue MakeCredentialValue(uint32_t index, const std::string& idp,
                                    bool update_rt = false) {
  std::string rt;
  if (update_rt) {
    rt = RefreshTokenString(index, idp) + "_new";
  } else {
    rt = RefreshTokenString(index, idp);
  }
  return CredentialValue(MakeCredentialIdentifier(index, idp), rt);
}

void AddRows(AuthDbFileImpl* db, uint32_t num) {
  // Add rows
  for (uint32_t i = 1; i <= num; i++) {
    EXPECT_EQ(Status::kOK,
              db->AddCredential(MakeCredentialValue(i, kGoogleIdp)));
  }
}

void VerifyRow(AuthDbFileImpl* db, uint32_t index,
               const std::string& idp = kGoogleIdp) {
  std::string refresh_token;
  auto cred_value = MakeCredentialValue(index, kGoogleIdp);
  EXPECT_EQ(Status::kOK,
            db->GetRefreshToken(cred_value.credential_id, &refresh_token));
  EXPECT_EQ(cred_value.refresh_token, refresh_token);
}

// Verify each row of the store.
void VerifyRows(AuthDbFileImpl* db, uint32_t num) {
  EXPECT_FALSE(db == nullptr);

  std::vector<CredentialValue> vals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(&vals));

  EXPECT_TRUE(vals.size() == num);

  for (uint32_t i = 1; i <= num; i++) {
    VerifyRow(db, i);
  }
}

}  // namespace

class AuthStoreTest : public ::testing::Test {
 protected:
  AuthStoreTest() {}

  ~AuthStoreTest() override {}

  virtual void SetUp() override {
    files::ScopedTempDir dir;
    ASSERT_TRUE(dir.NewTempFile(&tmp_creds_file_));

    uint64_t size;
    EXPECT_TRUE(files::GetFileSize(tmp_creds_file_, &size));
    EXPECT_EQ(0u, size);
  }

  virtual void TearDown() override {
    files::DeletePath(tmp_creds_file_, false);
  }

  std::string tmp_creds_file_;
};

TEST_F(AuthStoreTest, ParseEmptyAndExistingCreds) {
  // Test loading an empty file
  AuthDbFileImpl* db = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db->Load(), Status::kOK);

  AddRows(db, 3);
  VerifyRows(db, 3);

  // Test re-loading the file with existing users
  AuthDbFileImpl* db2 = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db2->Load(), Status::kOK);
  VerifyRows(db2, 3);
}

TEST_F(AuthStoreTest, AddUpdateAndDeleteSingleProvider) {
  AuthDbFileImpl* db = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db->Load(), Status::kOK);

  // Delete credential when the store is empty.
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(CredentialIdentifier("cred_1", kGoogleIdp)));

  // Add credential failure testcases
  EXPECT_EQ(Status::kInvalidArguments,
            db->AddCredential(
                CredentialValue(CredentialIdentifier("", kGoogleIdp),
                                "refresh_token")));  // empty idp credential id
  EXPECT_EQ(Status::kInvalidArguments,
            db->AddCredential(
                CredentialValue(CredentialIdentifier("idp_id_1", kTestIdp),
                                "")));  // empty refresh token

  // Add multiple
  AddRows(db, 3);

  VerifyRows(db, 3);

  // Update credential for the given IDP
  int updateIndex = 2;
  CredentialIdentifier updateCid =
      MakeCredentialIdentifier(updateIndex, kGoogleIdp);
  std::string old_refresh_token;
  std::string new_refresh_token;
  EXPECT_EQ(Status::kOK, db->GetRefreshToken(updateCid, &old_refresh_token));

  EXPECT_FALSE(old_refresh_token.empty());
  auto newCredVal = MakeCredentialValue(updateIndex, kGoogleIdp, true);
  EXPECT_EQ(Status::kOK, db->AddCredential(newCredVal));
  std::vector<CredentialValue> vals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(&vals));
  EXPECT_TRUE(vals.size() == 3);
  EXPECT_EQ(Status::kOK, db->GetRefreshToken(updateCid, &new_refresh_token));

  EXPECT_EQ(newCredVal.refresh_token, new_refresh_token);
  EXPECT_NE(old_refresh_token, new_refresh_token);

  // Delete credential for the given IDP
  int deleteIndex = 1;
  CredentialIdentifier deleteCid =
      MakeCredentialIdentifier(deleteIndex, kGoogleIdp);
  EXPECT_EQ(Status::kOK, db->GetRefreshToken(deleteCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());
  EXPECT_EQ(Status::kOK, db->DeleteCredential(deleteCid));
  std::vector<CredentialValue> remainingVals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(&remainingVals));
  EXPECT_TRUE(remainingVals.size() == 2);

  // Delete credential failure testcases
  // empty IDP cred id
  EXPECT_EQ(Status::kInvalidArguments,
            db->DeleteCredential(CredentialIdentifier("", kGoogleIdp)));
  // invalid IDP cred id
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(
                CredentialIdentifier("invalid_idp_user", kGoogleIdp)));
  // invalid IDP
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(
                CredentialIdentifier(IdpUserIdString(5, kTestIdp), kTestIdp)));
}

TEST_F(AuthStoreTest, AddUpdateAndDeleteMultipleProviders) {
  AuthDbFileImpl* db = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db->Load(), Status::kOK);

  // add multiple creds for each user
  for (uint32_t i = 1; i <= 5; i++) {
    EXPECT_EQ(Status::kOK,
              db->AddCredential(MakeCredentialValue(i, kGoogleIdp)));
    EXPECT_EQ(Status::kOK, db->AddCredential(MakeCredentialValue(i, kTestIdp)));
  }

  // Update TEST idp credential
  int updateIndex = 3;
  auto updateCid = MakeCredentialIdentifier(updateIndex, kTestIdp);
  std::string old_refresh_token;
  std::string new_refresh_token;

  EXPECT_EQ(Status::kOK, db->GetRefreshToken(updateCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());

  auto newVal = MakeCredentialValue(updateIndex, kTestIdp, true);
  EXPECT_EQ(Status::kOK, db->AddCredential(newVal));

  std::vector<CredentialValue> vals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(&vals));
  EXPECT_TRUE(vals.size() == 10);

  EXPECT_EQ(Status::kOK, db->GetRefreshToken(updateCid, &old_refresh_token));
  EXPECT_NE(old_refresh_token, new_refresh_token);

  // Make sure remaining values untouched
  for (uint32_t i = 1; i <= 5; i++) {
    VerifyRow(db, i, kGoogleIdp);
    if (i != 3) {
      VerifyRow(db, i, kTestIdp);
    }
  }

  // Delete TEST idp credential
  int deleteIndex = 5;
  CredentialIdentifier deleteCid =
      MakeCredentialIdentifier(deleteIndex, kTestIdp);
  EXPECT_EQ(Status::kOK, db->GetRefreshToken(deleteCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());
  EXPECT_EQ(Status::kOK, db->DeleteCredential(deleteCid));
  std::vector<CredentialValue> remainingVals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(&remainingVals));
  EXPECT_TRUE(remainingVals.size() == 9);

  // Make sure remaining values untouched
  for (uint32_t i = 1; i <= 5; i++) {
    VerifyRow(db, i, kGoogleIdp);
    if (i != 3 && i != 9) {
      VerifyRow(db, i, kTestIdp);
    }
  }
}

}  // namespace store
}  // namespace auth
