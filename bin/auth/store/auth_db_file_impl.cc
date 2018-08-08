// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth_db_file_impl.h"

#include <string>

#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/log_settings_command_line.h"

namespace auth {
namespace store {

namespace {

// Generates a flatbuffer |IdpCredential| instance for the given |idp| using the
// flatbuffer |builder|.
flatbuffers::Offset<::auth::IdpCredential> MakeIdpCredential(
    const std::string& idp_cred_id, const std::string& idp,
    const std::string& refresh_token, flatbuffers::FlatBufferBuilder* builder) {
  FXL_DCHECK(builder);
  FXL_DCHECK(!idp.empty());
  FXL_DCHECK(!idp_cred_id.empty());

  return ::auth::CreateIdpCredential(
      *builder, builder->CreateString(idp_cred_id), builder->CreateString(idp),
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
    FXL_LOG(ERROR) << "Unable to read user credentials file at: "
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

Status AuthDbFileImpl::AddCredential(const CredentialValue& val) {
  auto status = Validate(val.credential_id);
  if (status != Status::kOK) {
    return status;
  }

  if (val.refresh_token.empty()) {
    FXL_LOG(ERROR) << "Refresh token is empty";
    return Status::kInvalidArguments;
  }

  return UpdateDb(val.credential_id, val.refresh_token);
}

Status AuthDbFileImpl::DeleteCredential(
    const CredentialIdentifier& credential_id) {
  auto status = Validate(credential_id);
  if (status != Status::kOK) {
    return status;
  }

  return UpdateDb(credential_id, "");
}

Status AuthDbFileImpl::GetAllCredentials(
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
      credentials_out->push_back(CredentialValue(
          CredentialIdentifier(credential->id()->str(),
                               credential->identity_provider()->str()),
          credential->refresh_token()->str()));
    }
  }
  if (credentials_out->size() == 0) {
    return Status::kCredentialNotFound;
  }

  return Status::kOK;
}

Status AuthDbFileImpl::GetRefreshToken(
    const CredentialIdentifier& credential_id, std::string* refresh_token_out) {
  FXL_CHECK(refresh_token_out);

  auto status = Validate(credential_id);
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
    for (const auto* cred : *cred_store->creds()) {
      if (credential_id.identity_provider == cred->identity_provider()->str() &&
          credential_id.id == cred->id()->str()) {
        *refresh_token_out = cred->refresh_token()->str();
        return Status::kOK;
      }
    }
  }

  return Status::kCredentialNotFound;
}

Status AuthDbFileImpl::Validate(const CredentialIdentifier& credential_id) {
  if (!isLoaded) {
    FXL_LOG(ERROR) << "Load() must be called before invoking this api.";
    return Status::kDbNotInitialized;
  }

  if (credential_id.id.empty()) {
    FXL_LOG(ERROR) << "Idp user id is empty";
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

Status AuthDbFileImpl::UpdateDb(const CredentialIdentifier& credential_id,
                                const std::string& refresh_token) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<::auth::IdpCredential>> creds;

  auto idp = credential_id.identity_provider;

  bool delete_cred = refresh_token.empty();
  bool cred_found = false;

  if (!cred_store_buffer_.empty()) {
    // Get a pointer to the root object inside the buffer.
    auto cred_store = ::auth::GetCredentialStore(
        reinterpret_cast<const uint8_t*>(cred_store_buffer_.c_str()));
    if (cred_store != nullptr) {
      for (const auto* idp_cred : *cred_store->creds()) {
        if (idp == idp_cred->identity_provider()->str() &&
            credential_id.id == idp_cred->id()->str()) {
          cred_found = true;

          // Perform in-place update for an existing credential or delete it.
          if (!delete_cred) {
            creds.push_back(MakeIdpCredential(credential_id.id, idp,
                                              refresh_token, &builder));
          }
        } else {
          // Carry over existing credentials.
          creds.push_back(MakeIdpCredential(
              idp_cred->id()->str(), idp_cred->identity_provider()->str(),
              idp_cred->refresh_token()->str(), &builder));
        }
      }
    }
  }

  // Delete fails if the requested credential is not found.
  if (delete_cred && !cred_found) {
    return Status::kCredentialNotFound;
  }

  if (!delete_cred && !cred_found) {
    creds.push_back(
        MakeIdpCredential(credential_id.id, idp, refresh_token, &builder));
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
