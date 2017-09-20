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

using auth::store::IdentityProvider;

// Returns idp name for the given |IdentityProvider|.
std::string IdpString(IdentityProvider idp) {
  if (IdentityProvider::GOOGLE == idp) {
    return "goog";
  } else {
    return "test";
  }
}

// Generates a user id key string based on the given |index|.
std::string UserIdString(uint32_t index) {
  std::string out(15, 0);
  std::snprintf(&out[0], out.size(), "uid_%d", index);
  return out;
}

// Generates a idp userid string based on the given |index| and |idp|.
std::string IdpUserIdString(uint32_t index, IdentityProvider idp) {
  std::string out(15, 0);
  std::snprintf(&out[0], out.size(), "%s_user_%d", IdpString(idp).c_str(),
                index);
  return out;
}

// Generates a refresh token string based on the given |index| and |idp|.
std::string RefreshTokenString(uint32_t index, IdentityProvider idp) {
  std::string out(27, 0);
  std::snprintf(&out[0], out.size(), "%s_rt_%d", IdpString(idp).c_str(), index);
  return out;
}

// Returns a new CredentialIdentifer using the given |index| and |idp|.
CredentialIdentifier MakeCredentialIdentifier(uint32_t index,
                                              IdentityProvider idp) {
  return CredentialIdentifier(IdpUserIdString(index, idp), idp);
}

// Returns a new CredentialValue by generating a new refresh token based on the
// given |index| and |idp| values.
CredentialValue MakeCredentialValue(uint32_t index,
                                    IdentityProvider idp,
                                    bool update_rt = false) {
  std::string rt;
  if (update_rt) {
    rt = RefreshTokenString(index, idp) + "_new";
  } else {
    rt = RefreshTokenString(index, idp);
  }
  return CredentialValue(MakeCredentialIdentifier(index, idp), rt);
}

// Verify each row of the store.
void VerifyRows(AuthDbFileImpl* db) {
  EXPECT_FALSE(db == nullptr);

  for (int i = 1; i <= 10; i++) {
    std::vector<CredentialValue> vals;
    EXPECT_EQ(Status::kOK, db->GetAllCredentials(UserIdString(i), &vals));

    EXPECT_TRUE(vals.size() == 1);
    for (auth::store::CredentialValue cred : vals) {
      EXPECT_EQ(cred.credential_id.id,
                IdpUserIdString(i, IdentityProvider::GOOGLE));
      EXPECT_EQ(cred.credential_id.identity_provider, IdentityProvider::GOOGLE);
      EXPECT_EQ(cred.refresh_token,
                RefreshTokenString(i, IdentityProvider::GOOGLE));
    }
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

  // Add rows
  for (uint32_t i = 1; i <= 10; i++) {
    EXPECT_EQ(Status::kOK, db->AddCredential(UserIdString(i),
                                             MakeCredentialValue(
                                                 i, IdentityProvider::GOOGLE)));
  }
  VerifyRows(db);

  // Test re-loading the file with existing users
  AuthDbFileImpl* db2 = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db2->Load(), Status::kOK);
  VerifyRows(db2);
}

TEST_F(AuthStoreTest, AddUpdateAndDeleteSingleCred) {
  AuthDbFileImpl* db = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db->Load(), Status::kOK);

  // Delete credential when the store is empty.
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(
                "5", CredentialIdentifier("cred_1", IdentityProvider::GOOGLE)));

  // Add credential failure testcases
  EXPECT_EQ(Status::kInvalidArguments,
            db->AddCredential(
                "", MakeCredentialValue(
                        999, IdentityProvider::GOOGLE)));  // empty user id
  EXPECT_EQ(
      Status::kInvalidArguments,
      db->AddCredential(
          "test_id_1",
          CredentialValue(CredentialIdentifier("", IdentityProvider::GOOGLE),
                          "refresh_token")));  // empty idp credential id
  EXPECT_EQ(
      Status::kInvalidArguments,
      db->AddCredential("test_id_1",
                        CredentialValue(CredentialIdentifier(
                                            "idp_id_1", IdentityProvider::TEST),
                                        "")));  // empty refresh token

  // Add single user cred for each user
  for (uint32_t i = 1; i <= 10; i++) {
    EXPECT_EQ(Status::kOK, db->AddCredential(UserIdString(i),
                                             MakeCredentialValue(
                                                 i, IdentityProvider::GOOGLE)));
  }

  VerifyRows(db);

  // Update credential for the given IDP
  int updateIndex = 7;
  std::string updateUserId = UserIdString(updateIndex);
  CredentialIdentifier updateCid =
      MakeCredentialIdentifier(updateIndex, IdentityProvider::GOOGLE);
  std::string old_refresh_token;
  EXPECT_EQ(Status::kOK,
            db->GetRefreshToken(updateUserId, updateCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());
  auto newCredVal =
      MakeCredentialValue(updateIndex, IdentityProvider::GOOGLE, true);
  EXPECT_EQ(Status::kOK, db->AddCredential(updateUserId, newCredVal));
  std::vector<CredentialValue> vals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(updateUserId, &vals));
  EXPECT_TRUE(vals.size() == 1);
  EXPECT_TRUE(newCredVal == vals[0]);
  EXPECT_TRUE(old_refresh_token != vals[0].refresh_token);

  // Delete credential for the given IDP
  int deleteIndex = 5;
  std::string deleteUserId = UserIdString(deleteIndex);
  CredentialIdentifier deleteCid =
      MakeCredentialIdentifier(deleteIndex, IdentityProvider::GOOGLE);
  EXPECT_EQ(Status::kOK,
            db->GetRefreshToken(deleteUserId, deleteCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());
  EXPECT_EQ(Status::kOK, db->DeleteCredential(deleteUserId, deleteCid));
  std::vector<CredentialValue> deletedVals;
  EXPECT_EQ(Status::kCredentialNotFound,
            db->GetAllCredentials(deleteUserId, &deletedVals));
  EXPECT_TRUE(deletedVals.size() == 0);

  // Delete credential failure testcases
  // empty user id
  EXPECT_EQ(Status::kInvalidArguments, db->DeleteCredential("", deleteCid));
  // empty IDP cred id
  EXPECT_EQ(
      Status::kInvalidArguments,
      db->DeleteCredential(deleteUserId,
                           CredentialIdentifier("", IdentityProvider::GOOGLE)));
  // invalid IDP cred id
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(
                deleteUserId, CredentialIdentifier("invalid_user",
                                                   IdentityProvider::GOOGLE)));
  // invalid IDP cred id which pointsto a different user
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(
                deleteUserId, CredentialIdentifier(
                                  IdpUserIdString(6, IdentityProvider::GOOGLE),
                                  IdentityProvider::GOOGLE)));
  // invalid IDP
  EXPECT_EQ(Status::kCredentialNotFound,
            db->DeleteCredential(
                deleteUserId,
                CredentialIdentifier(IdpUserIdString(5, IdentityProvider::TEST),
                                     IdentityProvider::TEST)));
}

TEST_F(AuthStoreTest, AddUpdateAndDeleteMultipleCreds) {
  AuthDbFileImpl* db = new AuthDbFileImpl(tmp_creds_file_);
  EXPECT_EQ(db->Load(), Status::kOK);

  // add multiple creds for each user
  for (uint32_t i = 1; i <= 5; i++) {
    std::string id = UserIdString(i);
    EXPECT_EQ(Status::kOK,
              db->AddCredential(
                  id, MakeCredentialValue(i, IdentityProvider::GOOGLE)));
    EXPECT_EQ(
        Status::kOK,
        db->AddCredential(id, MakeCredentialValue(i, IdentityProvider::TEST)));
  }

  // Update TEST idp credential
  int updateIndex = 3;
  std::string updateUserId = UserIdString(updateIndex);
  auto updateCid =
      MakeCredentialIdentifier(updateIndex, IdentityProvider::TEST);
  std::string old_refresh_token;
  EXPECT_EQ(Status::kOK,
            db->GetRefreshToken(updateUserId, updateCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());

  auto newVal = MakeCredentialValue(updateIndex, IdentityProvider::TEST, true);
  EXPECT_EQ(Status::kOK, db->AddCredential(updateUserId, newVal));

  std::vector<CredentialValue> vals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(updateUserId, &vals));
  EXPECT_TRUE(vals.size() == 2);
  for (auto val : vals) {
    if (val.credential_id.identity_provider == IdentityProvider::TEST) {
      EXPECT_TRUE(val == newVal);
      EXPECT_TRUE(val.refresh_token != old_refresh_token);
    } else {
      EXPECT_TRUE(val ==
                  MakeCredentialValue(updateIndex, IdentityProvider::GOOGLE));
    }
  }

  // Delete TEST idp credential
  int deleteIndex = 5;
  std::string deleteUserId = UserIdString(deleteIndex);
  CredentialIdentifier deleteCid =
      MakeCredentialIdentifier(deleteIndex, IdentityProvider::TEST);
  EXPECT_EQ(Status::kOK,
            db->GetRefreshToken(deleteUserId, deleteCid, &old_refresh_token));
  EXPECT_FALSE(old_refresh_token.empty());
  EXPECT_EQ(Status::kOK, db->DeleteCredential(deleteUserId, deleteCid));
  std::vector<CredentialValue> deletedVals;
  EXPECT_EQ(Status::kOK, db->GetAllCredentials(deleteUserId, &deletedVals));
  EXPECT_TRUE(deletedVals.size() == 1);
}

}  // namespace store
}  // namespace auth

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
