// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_AUTH_SRC_STORE_AUTH_DB_H
#define APPS_AUTH_SRC_STORE_AUTH_DB_H

#include <memory>
#include <string>
#include <vector>

namespace auth {
namespace store {

// The status of an operation.
enum Status {
  // The operation succeeded.
  kOK = 0,

  // The operation was not attempted because the arguments are invalid.
  kInvalidArguments,

  // The operation was attempted but failed for an unspecified reason. More
  // information may be found in the log file.
  kOperationFailed,

  // The operation was not attempted because there is an error in configuration.
  kConfigRequired
};

enum IdentityProvider { GOOGLE = 0, TEST };

// Uniquely identifies a given user credential using an identifier |id| provided
// by the Identity Provider |identity_provider|.
struct CredentialIdentifier {
  std::string id;
  IdentityProvider identity_provider;

  // Constructor
  CredentialIdentifier(std::string id, IdentityProvider identity_provider)
      : id(std::move(id)), identity_provider(std::move(identity_provider)) {}
};

// The value of a column contained in a |Row|.
struct CredentialValue {
  CredentialIdentifier credential_id;
  std::string refresh_token;

  // Constructor
  CredentialValue(CredentialIdentifier credential_id, std::string refresh_token)
      : credential_id(std::move(credential_id)),
        refresh_token(std::move(refresh_token)) {}

  bool operator==(const CredentialValue& other) const {
    if (credential_id.id == other.credential_id.id &&
        credential_id.identity_provider ==
            other.credential_id.identity_provider &&
        refresh_token == other.refresh_token) {
      return true;
    }

    return false;
  }
};

// Interface to underlying user credentials store.
//
// User Credentials store is a key value store. Unique account ids are used as
// keys, with a serialized string of IDP and the refresh token as the value.
// The rows are ordered using FIFO.
class AuthDb {
 public:
  AuthDb(){};

  virtual ~AuthDb() = 0;

  // Adds a new user credential to auth db. The operation may be an insert of a
  // new user or a replacement of existing user credential.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status AddCredential(const std::string& account_id,
                               const CredentialValue& val) = 0;

  // Deletes an existing user credential identified by |credential_id| from auth
  // db.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status DeleteCredential(
      const std::string& account_id,
      const CredentialIdentifier& credential_id) = 0;

  // Returns a vector of all provisioned users from the underlying auth db.
  //
  // Returns kOK on success or an error status on failure.
  virtual std::vector<CredentialValue> GetCredentials(
      const std::string& account_id) = 0;

  // Returns the refresh token credential for the user |account_id| and the
  // given identity provider |idp|.
  //
  // Returns an empty token on failure.
  virtual CredentialValue GetRefreshToken(const std::string& account_id,
                                          const IdentityProvider idp) = 0;
};

}  // namespace store
}  // namespace auth

#endif  // APPS_AUTH_SRC_STORE_AUTH_DB_H
