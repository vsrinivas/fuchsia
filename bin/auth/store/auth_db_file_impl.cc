// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth_db_file_impl.h"

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
      return auth::store::IdentityProvider::TEST;
  }
}

// Generates a flatbuffer |IdpCredential| instance for the given |idp| using the
// flatbuffer |builder|.
flatbuffers::Offset<::auth::IdpCredential> MakeIdpCredential(
    const std::string& idp_cred_id,
    const ::auth::IdentityProvider idp,
    const std::string& refresh_token,
    flatbuffers::FlatBufferBuilder* builder) {
  FXL_DCHECK(builder);
  FXL_DCHECK(!idp_cred_id.empty());

  return ::auth::CreateIdpCredential(*builder,
                                     builder->CreateString(idp_cred_id), idp,
                                     builder->CreateString(refresh_token));
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
    FXL_LOG(WARNING) << "Unable to read user credentials file at: "
                     << credentials_file_;
    return Status::kOperationFailed;
  }

  auto status = VerifyCredentials(serialized_creds);
  if (status != Status::kOK) {
    return status;
  }

  // Save credentials to in-memory cache |cred_store_buffer_|.
  cred_store_buffer_.swap(serialized_creds);

  isLoaded = true;
  return Status::kOK;
}

Status AuthDbFileImpl::AddCredential(const std::string& user_id,
                                     const CredentialValue& val) {
  auto status = Validate(user_id, val.credential_id);
  if (status != Status::kOK) {
    return status;
  }

  if (val.refresh_token.empty()) {
    FXL_LOG(ERROR) << "Refresh token is empty for user id: " << user_id;
    return Status::kInvalidArguments;
  }

  return UpdateDb(user_id, val.credential_id, val.refresh_token);
}

Status AuthDbFileImpl::DeleteCredential(
    const std::string& user_id,
    const CredentialIdentifier& credential_id) {
  auto status = Validate(user_id, credential_id);
  if (status != Status::kOK) {
    return status;
  }

  return UpdateDb(user_id, credential_id, "");
}

Status AuthDbFileImpl::GetAllCredentials(
    const std::string& user_id,
    std::vector<CredentialValue>* credentials_out) {
  FXL_CHECK(credentials_out);
  credentials_out->clear();

  if (!isLoaded) {
    FXL_LOG(ERROR) << "Load() must be called before invoking this api.";
    return Status::kDbNotInitialized;
  }

  // Get a pointer to the root object inside the buffer.
  auto cred_store = ::auth::GetCredentialStore(
      reinterpret_cast<const uint8_t*>(cred_store_buffer_.c_str()));

  if (cred_store != nullptr) {
    for (const auto* credential : *cred_store->creds()) {
      if (credential->user_id()->str() == user_id) {
        for (const auto* token : *credential->tokens()) {
          credentials_out->push_back(CredentialValue(
              CredentialIdentifier(
                  token->id()->str(),
                  MapToAuthIdentityProvider(token->identity_provider())),
              token->refresh_token()->str()));
        }
        break;
      }
    }
  }
  if (credentials_out->size() == 0) {
    return Status::kCredentialNotFound;
  }

  return Status::kOK;
}

Status AuthDbFileImpl::GetRefreshToken(
    const std::string& user_id,
    const CredentialIdentifier& credential_id,
    std::string* refresh_token_out) {
  FXL_CHECK(refresh_token_out);

  auto status = Validate(user_id, credential_id);
  if (status != Status::kOK) {
    return status;
  }

  if (cred_store_buffer_.empty()) {
    return Status::kCredentialNotFound;
  }

  // Get a pointer to the root object inside the buffer.
  auto cred_store = ::auth::GetCredentialStore(
      reinterpret_cast<const uint8_t*>(cred_store_buffer_.c_str()));

  if (cred_store != nullptr) {
    auth::IdentityProvider fbs_idp =
        MapToFbsIdentityProvider(credential_id.identity_provider);

    for (const auto* credential : *cred_store->creds()) {
      if (credential->user_id()->str() == user_id) {
        for (const auto* token : *credential->tokens()) {
          if (fbs_idp == token->identity_provider() &&
              credential_id.id == token->id()->str()) {
            *refresh_token_out = token->refresh_token()->str();
            return Status::kOK;
          }
        }
      }
    }
  }

  return Status::kCredentialNotFound;
}

Status AuthDbFileImpl::Validate(const std::string& user_id,
                                const CredentialIdentifier& credential_id) {
  if (!isLoaded) {
    FXL_LOG(ERROR) << "Load() must be called before invoking this api.";
    return Status::kDbNotInitialized;
  }

  if (user_id.empty()) {
    FXL_LOG(ERROR) << "User id is empty.";
    return Status::kInvalidArguments;
  }

  if (credential_id.id.empty()) {
    FXL_LOG(ERROR) << "Idp user id is empty for user id: " << user_id;
    return Status::kInvalidArguments;
  }

  return Status::kOK;
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
  FXL_DCHECK(VerifyCredentials(serialized_creds) == Status::kOK);
  FXL_DCHECK(files::IsDirectory(files::GetDirectoryName(credentials_file_)));

  if (!files::WriteFileInTwoPhases(
          credentials_file_, serialized_creds,
          files::GetDirectoryName(credentials_file_))) {
    FXL_LOG(ERROR) << "Unable to write file " << credentials_file_;
    return Status::kOperationFailed;
  }

  return Status::kOK;
}

Status AuthDbFileImpl::UpdateDb(const std::string& user_id,
                                const CredentialIdentifier& credential_id,
                                const std::string& refresh_token) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<::auth::UserCredential>> creds;

  auth::IdentityProvider idp =
      MapToFbsIdentityProvider(credential_id.identity_provider);

  bool user_found = false;
  bool update_cred = !refresh_token.empty();

  // Reserialize existing users.
  if (!cred_store_buffer_.empty()) {
    // Get a pointer to the root object inside the buffer.
    auto cred_store = ::auth::GetCredentialStore(
        reinterpret_cast<const uint8_t*>(cred_store_buffer_.c_str()));

    if (cred_store != nullptr) {
      // TODO(ukode): Sort userids for future optimization.
      for (const auto* cred : *cred_store->creds()) {
        std::vector<flatbuffers::Offset<::auth::IdpCredential>> idp_creds;

        if (cred->user_id()->str() != user_id) {
          for (const auto* idp_cred : *cred->tokens()) {
            idp_creds.push_back(MakeIdpCredential(
                idp_cred->id()->str(), idp_cred->identity_provider(),
                idp_cred->refresh_token()->str(), &builder));
          }
        } else {
          user_found = true;

          bool cred_exists = false;
          for (const auto* idp_cred : *cred->tokens()) {
            if (idp == idp_cred->identity_provider() &&
                credential_id.id == idp_cred->id()->str()) {
              cred_exists = true;

              // Perform in-place update for an existing credential.
              if (update_cred) {
                idp_creds.push_back(MakeIdpCredential(credential_id.id, idp,
                                                      refresh_token, &builder));
              }
            } else {
              // Carry over existing credentials.
              idp_creds.push_back(MakeIdpCredential(
                  idp_cred->id()->str(), idp_cred->identity_provider(),
                  idp_cred->refresh_token()->str(), &builder));
            }
          }

          // Add new IDP credential to the existing credentials.
          if (!cred_exists && update_cred) {
            idp_creds.push_back(MakeIdpCredential(credential_id.id, idp,
                                                  refresh_token, &builder));
          }

          // Delete fails if the requested credential is not found.
          if (!cred_exists && !update_cred) {
            return Status::kCredentialNotFound;
          }
        }

        if (!idp_creds.empty()) {
          creds.push_back(::auth::CreateUserCredential(
            builder, builder.CreateString(cred->user_id()),
            builder.CreateVector<flatbuffers::Offset<::auth::IdpCredential>>(
                idp_creds)));
        }
      }
    }
  }

  // Delete fails if the requested user is not found.
  if (!user_found && !update_cred) {
    return Status::kCredentialNotFound;
  }

  // Handle first-time user by adding a new UserCredential.
  if (!user_found && update_cred) {
    std::vector<flatbuffers::Offset<::auth::IdpCredential>> new_idp_creds;
    new_idp_creds.push_back(
        MakeIdpCredential(credential_id.id, idp, refresh_token, &builder));

    creds.push_back(::auth::CreateUserCredential(
        builder, builder.CreateString(user_id),
        builder.CreateVector<flatbuffers::Offset<::auth::IdpCredential>>(
            new_idp_creds)));
  }

  builder.Finish(
      ::auth::CreateCredentialStore(builder, builder.CreateVector(creds)));

  // Save current credentials to in-memory cache |creds_store_buffer_| for other
  // callers to use.
  auto bufferpointer =
      reinterpret_cast<const char*>(builder.GetBufferPointer());
  cred_store_buffer_.assign(bufferpointer, builder.GetSize());

  return Commit(cred_store_buffer_);
}

}  // namespace store
}  // namespace auth
