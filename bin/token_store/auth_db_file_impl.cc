// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/auth/src/store/auth_db_file_impl.h"

#include <string>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/log_settings_command_line.h"

namespace auth {
namespace store {

namespace {

// Converts |CredentialValue.identity_provider| to |IdentityProvider| defined in
// credentials.fbs.
auth::IdentityProvider MapToFbsIdentityProvider(
    auth::store::IdentityProvider idp) {
  switch (idp) {
    case auth::store::IdentityProvider::GOOGLE:
      return IdentityProvider_GOOGLE;
    case auth::store::IdentityProvider::TEST:
    default:
      return IdentityProvider_TEST;
  }
}

// Converts |IdentityProvider| defined in credentials.fbs to
// |CredentialValue.identity_provider| value.
auth::store::IdentityProvider MapToAuthIdentityProvider(
    auth::IdentityProvider idp) {
  switch (idp) {
    case IdentityProvider_GOOGLE:
      return auth::store::IdentityProvider::GOOGLE;
    case IdentityProvider_TEST:
    default:
      return auth::store::IdentityProvider::TEST;
  }
}

}  // end namespace

AuthDbFileImpl::AuthDbFileImpl(const std::string& credentials_file)
    : credentials_file_(credentials_file) {}

AuthDbFileImpl::~AuthDbFileImpl() {}

Status AuthDbFileImpl::Load() {
  if (isLoaded) {
    return Status::kOK;
  }

  if (!files::IsFile(credentials_file_)) {
    // System is just bootstrapping, no users provisioned so far.
    if (!files::CreateDirectory(files::GetDirectoryName(credentials_file_))) {
      return Status::kOperationFailed;
    }
    isLoaded = true;
    return Status::kOK;
  }

  // Reserialize existing users.
  std::string serialized_creds;
  if (!files::ReadFileToString(credentials_file_, &serialized_creds)) {
    FXL_LOG(WARNING) << "Unable to read user configuration file at: "
                     << credentials_file_;
    return Status::kOperationFailed;
  }

  auto status = VerifyCredentials(serialized_creds);
  if (status != Status::kOK) {
    return status;
  }

  this->cred_store_ = ::auth::GetCredentialStore(serialized_creds.data());
  isLoaded = true;
  return Status::kOK;
}

Status AuthDbFileImpl::AddCredential(const std::string& account_id,
                                     const CredentialValue& val) {
  if (!isLoaded) {
    FXL_LOG(ERROR) << "Load() must be called before invoking this api.";
    return Status::kConfigRequired;
  }

  if (account_id.empty()) {
    FXL_LOG(ERROR) << "Account id is empty.";
    return Status::kOperationFailed;
  }

  if (val.refresh_token.empty()) {
    FXL_LOG(ERROR) << "Refresh token is empty for account id: " << account_id;
    return Status::kOperationFailed;
  }

  return UpdateDb(account_id, val.credential_id, val.refresh_token);
}

Status AuthDbFileImpl::DeleteCredential(
    const std::string& account_id,
    const CredentialIdentifier& credential_id) {
  if (!isLoaded) {
    FXL_LOG(ERROR) << "Load() must be called before invoking this api.";
    return Status::kConfigRequired;
  }

  if (account_id.empty()) {
    FXL_LOG(ERROR) << "Account id is empty.";
    return Status::kOperationFailed;
  }

  if (credential_id.id.empty()) {
    FXL_LOG(ERROR) << "Idp user id is empty for account id: " << account_id;
    return Status::kOperationFailed;
  }

  return UpdateDb(account_id, credential_id, "");
}

std::vector<CredentialValue> AuthDbFileImpl::GetCredentials(
    const std::string& account_id) {
  FXL_DCHECK(isLoaded);
  std::vector<CredentialValue> vals;

  if (cred_store_ != nullptr) {
    for (const auto* credential : *cred_store_->creds()) {
      if (credential->account_id()->str() == account_id) {
        for (const auto* token : *credential->tokens()) {
          vals.push_back(CredentialValue(
              CredentialIdentifier(
                  token->id()->str(),
                  MapToAuthIdentityProvider(token->identity_provider())),
              token->refresh_token()->str()));
        }
      }
    }
  }
  return vals;
}

CredentialValue AuthDbFileImpl::GetRefreshToken(
    const std::string& account_id,
    const auth::store::IdentityProvider idp) {
  FXL_DCHECK(isLoaded);
  FXL_DCHECK(!account_id.empty());

  if (cred_store_ != nullptr) {
    auth::IdentityProvider fbs_idp = MapToFbsIdentityProvider(idp);

    for (const auto* credential : *cred_store_->creds()) {
      if (credential->account_id()->str() == account_id) {
        for (const auto* token : *credential->tokens()) {
          if (fbs_idp == token->identity_provider()) {
            return CredentialValue(
                CredentialIdentifier(token->id()->str(), idp),
                token->refresh_token()->str());
          }
        }
      }
    }
  }

  return CredentialValue(CredentialIdentifier("", idp), "");
}

Status AuthDbFileImpl::VerifyCredentials(const std::string& serialized_creds) {
  // verify file before saving
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(serialized_creds.data()),
      serialized_creds.size());

  if (!::auth::VerifyCredentialStoreBuffer(verifier)) {
    FXL_LOG(ERROR) << "Unable to verify credentials buffer:"
                   << serialized_creds.data();
    return Status::kOperationFailed;
  }

  return Status::kOK;
}

Status AuthDbFileImpl::Commit(const std::string& serialized_creds) {
  auto status = VerifyCredentials(serialized_creds);
  FXL_DCHECK(status == Status::kOK);

  FXL_DCHECK(!files::GetDirectoryName(credentials_file_).empty());

  if (!files::WriteFile(credentials_file_, serialized_creds.data(),
                        serialized_creds.size())) {
    FXL_LOG(ERROR) << "Unable to write file " << credentials_file_;
    return Status::kOperationFailed;
  }

  return Status::kOK;
}

Status AuthDbFileImpl::UpdateDb(const std::string& account_id,
                                const CredentialIdentifier& credential_id,
                                const std::string& refresh_token) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<::auth::UserCredential>> creds;

  auth::IdentityProvider idp =
      MapToFbsIdentityProvider(credential_id.identity_provider);

  // Reserialize existing users.
  if (cred_store_ != nullptr) {
    for (const auto* cred : *cred_store_->creds()) {
      std::vector<flatbuffers::Offset<::auth::IdpCredential>> idp_creds;
      for (const auto* idp_cred : *cred->tokens()) {
        if (cred->account_id()->str() == account_id &&
            idp == idp_cred->identity_provider() &&
            credential_id.id == idp_cred->id()->str()) {
          // Update or delete existing credentials
          continue;
        }

        idp_creds.push_back(::auth::CreateIdpCredential(
            builder, builder.CreateString(idp_cred->id()),
            idp_cred->identity_provider(),
            builder.CreateString(idp_cred->refresh_token())));
      }

      creds.push_back(::auth::CreateUserCredential(
          builder, builder.CreateString(cred->account_id()),
          builder.CreateVector<flatbuffers::Offset<::auth::IdpCredential>>(
              idp_creds)));
    }
  }

  if (!refresh_token.empty()) {
    // add the new credential for |account_->id|.
    std::vector<flatbuffers::Offset<::auth::IdpCredential>> new_idp_creds;
    new_idp_creds.push_back(::auth::CreateIdpCredential(
        builder, builder.CreateString(credential_id.id), idp,
        builder.CreateString(refresh_token)));

    creds.push_back(::auth::CreateUserCredential(
        builder, builder.CreateString(account_id),
        builder.CreateVector<flatbuffers::Offset<::auth::IdpCredential>>(
            new_idp_creds)));
  }

  builder.Finish(
      ::auth::CreateCredentialStore(builder, builder.CreateVector(creds)));
  std::string new_serialized_creds = std::string(
      reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
      builder.GetSize());

  // Add new credentials to in-memory cache |creds_store_|.
  cred_store_ = ::auth::GetCredentialStore(new_serialized_creds.data());

  return Commit(new_serialized_creds);
}

}  // namespace store
}  // namespace auth
