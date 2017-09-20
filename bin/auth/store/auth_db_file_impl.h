// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_STORE_AUTH_DB_FILE_IMPL_H
#define GARNET_BIN_AUTH_STORE_AUTH_DB_FILE_IMPL_H

#include <string>

#include "auth_db.h"
#include "garnet/bin/auth/store/credentials_generated.h"

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
  Status AddCredential(const std::string& user_id,
                       const CredentialValue& val) override;

  // Deletes an existing user credential identified by |credential_id| from auth
  // db.
  //
  // Returns kOK on success or an error status on failure.
  Status DeleteCredential(const std::string& user_id,
                          const CredentialIdentifier& credential_id) override;

  // Fetches list of all credentials provisioned for user |user_id| with
  // different Identity Providers in |credentials_out|.
  //
  // Returns kOK on success or an error status on failure.
  Status GetAllCredentials(
      const std::string& user_id,
      std::vector<CredentialValue>* credentials_out) override;

  // Fetches |refresh_token| from the token store for the given user with unique
  // |user_id| and identity provider |idp|.
  //
  // Returns kOK on success or an error status on failure.
  Status GetRefreshToken(const std::string& user_id,
                         const CredentialIdentifier& credential_id,
                         std::string* refresh_token) override;

 private:
  // Validates input keys |user_id| and |credential_id| and checks if the
  // in-memory credential database has been initialized successfully by
  // invoking Load() before any operation.
  //
  // Returns kOK status if input is valid or an error status for invalid input
  // or if the in-memory credentials database is not initialized.
  Status Validate(const std::string& user_id,
                  const CredentialIdentifier& credential_id);

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
  // update or delete of the existing user credential. If |refresh_token| is
  // empty, credential is wiped from the store. Otherwise, refresh token is
  // saved as a new credential or upgraded in-place for an existing credential.
  //
  // Returns kOK on success or an error status on failure.
  Status UpdateDb(const std::string& user_id,
                  const CredentialIdentifier& credential_id,
                  const std::string& refresh_token);

  // In-memory buffer for storing serialized credential store contents.
  std::string cred_store_buffer_;
  std::string credentials_file_;
  bool isLoaded = false;
};

}  // namespace store
}  // namespace auth

#endif  // GARNET_BIN_AUTH_STORE_AUTH_DB_FILE_IMPL_H
