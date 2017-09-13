// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_AUTH_SRC_STORE_AUTH_DB_FILE_IMPL_H
#define APPS_AUTH_SRC_STORE_AUTH_DB_FILE_IMPL_H

#include <string>

#include "auth_db.h"
#include "garnet/bin/token_store/credentials_generated.h"

namespace auth {
namespace store {

// Implementation of AuthDB as serialized flatbuffer files. The schema of the
// credential database is defined in |credentials.fbs|.
class AuthDbFileImpl : public AuthDb {
 public:
  AuthDbFileImpl(const std::string& credentials_file);

  ~AuthDbFileImpl() override;

  // Deserializes |credentials_file_| contents on disk to in-memory
  // |cred_store_|. Load() should be called before invoking other apis.
  //
  // Returns kOK on success or an error status on failure.
  Status Load();

  // Adds a new user credential to auth db. The operation may be an insert of a
  // new user or a replacement of existing user credential.
  //
  // Returns kOK on success or an error status on failure.
  Status AddCredential(const std::string& account_id,
                       const CredentialValue& val) override;

  // Deletes an existing user credential identified by |credential_id| from auth
  // db.
  //
  // Returns kOK on success or an error status on failure.
  Status DeleteCredential(const std::string& account_id,
                          const CredentialIdentifier& credential_id) override;

  // Returns a vector of all provisioned users from the underlying auth db.
  //
  // Returns kOK on success or an error status on failure.
  std::vector<CredentialValue> GetCredentials(
      const std::string& account_id) override;

  // Returns the refresh token credential for the user |account_id| and the
  // given identity provider |idp|.
  //
  // Returns an empty token on failure.
  CredentialValue GetRefreshToken(
      const std::string& account_id,
      const auth::store::IdentityProvider idp) override;

 private:
  // Verifies |serialized_creds| flatbuffer during a read or a write operation
  // to |credentials_file_|.
  //
  // Returns kOK on success or an error status on failure.
  Status VerifyCredentials(const std::string& serialized_creds);

  // Serializes |cred_store_| flatbuffer to |credentials_file_| on disk.
  //
  // Returns kOK on success or an error status on failure.
  Status Commit(const std::string& serialized_creds);

  // Modifies user credential stored in auth db. The operation may be an insert,
  // update or delete of the existing user credential.
  //
  // Returns kOK on success or an error status on failure.
  Status UpdateDb(const std::string& account_id,
                  const CredentialIdentifier& credential_id,
                  const std::string& refresh_token);

  const ::auth::CredentialStore* cred_store_ = nullptr;
  std::string credentials_file_;
  bool isLoaded = false;
};

}  // namespace store
}  // namespace auth

#endif  // APPS_AUTH_SRC_STORE_AUTH_DB_FILE_IMPL_H
