// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_STORE_AUTH_DB_H
#define GARNET_BIN_AUTH_STORE_AUTH_DB_H

#include <memory>
#include <string>
#include <vector>

// AuthDb provides an interface to underlying user credentials store.
//
// User Credentials store is a key value store. Unique user_ids provided by
// the caller are used as keys. Each user_id maps to a list of credentials. Each
// credential is an OAuth refresh token bound to the Identity Provider that
// issues it and is identified by a unique identifier such as email address or
// user's profile url as provided by the Identity Provider during the OAuth
// handshake.

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

  // The operation was not attempted because there is an error in loading
  // existing DB state.
  kDbNotInitialized,

  // The operation was attempted but failed as the requested credential is not
  // configured in the system.
  kCredentialNotFound
};

enum IdentityProvider { GOOGLE = 0, TEST };

// Uniquely identifies a given user credential using an identifier |id| provided
// by the Identity Provider |identity_provider|. The identifier here refers to
// user's email address or profile url as configured on the Identity Provider's
// backend.
struct CredentialIdentifier {
  std::string id;
  IdentityProvider identity_provider;

  CredentialIdentifier(std::string id, IdentityProvider identity_provider)
      : id(std::move(id)), identity_provider(identity_provider) {}

  bool operator==(const CredentialIdentifier& other) const {
    if (id == other.id && identity_provider == other.identity_provider) {
      return true;
    }

    return false;
  }
};

// Value of each credential contains a unique identifier and an OAuth refresh
// token string provided by the Identity Provider.
struct CredentialValue {
  CredentialIdentifier credential_id;
  std::string refresh_token;

  CredentialValue(CredentialIdentifier credential_id, std::string refresh_token)
      : credential_id(std::move(credential_id)),
        refresh_token(std::move(refresh_token)) {}

  bool operator==(const CredentialValue& other) const {
    if (credential_id == other.credential_id &&
        refresh_token == other.refresh_token) {
      return true;
    }

    return false;
  }
};

// Interface to underlying user credentials store.
class AuthDb {
 public:
  AuthDb(){};

  virtual ~AuthDb(){};

  // Adds a new user credential to auth db. The operation may be an insert of a
  // new user or a replacement of existing user credential. Replacement of an
  // existing credential takes place when the credential gets refreshed either
  // because it has expired or invalidated by the Identity Provider.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status AddCredential(const std::string& user_id,
                               const CredentialValue& val) = 0;

  // Deletes an existing user credential identified by |credential_id| from auth
  // db.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status DeleteCredential(
      const std::string& user_id,
      const CredentialIdentifier& credential_id) = 0;

  // Fetches list of all credentials provisioned for user |user_id| with
  // different Identity Providers in |credentials_out|.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status GetAllCredentials(
      const std::string& user_id,
      std::vector<CredentialValue>* credentials_out) = 0;

  // Fetches |refresh_token| from the token store for the given user with unique
  // |user_id| and identity provider |idp|.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status GetRefreshToken(const std::string& user_id,
                                 const CredentialIdentifier& credential_id,
                                 std::string* refresh_token) = 0;
};

}  // namespace store
}  // namespace auth

#endif  // GARNET_BIN_AUTH_STORE_AUTH_DB_H
