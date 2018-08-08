// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///////////////////////////////////////////////////////////
// class DevAuthProviderImpl
///////////////////////////////////////////////////////////

// This application is intented to be used as a headless auth provider for
// testing of the Token Manager service in Garnet layer.
//
// It also serves as an example of how to use the Auth Provider FIDL interface.

#ifndef GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_DEV_AUTH_PROVIDER_IMPL_H_
#define GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_DEV_AUTH_PROVIDER_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

namespace auth {
namespace dev_auth_provider {

using fuchsia::auth::AssertionJWTParams;
using fuchsia::auth::AttestationJWTParams;
using fuchsia::auth::AttestationSigner;
using fuchsia::auth::AuthenticationUIContext;

class DevAuthProviderImpl : public fuchsia::auth::AuthProvider,
                            fuchsia::auth::AttestationSigner {
 public:
  DevAuthProviderImpl();

  ~DevAuthProviderImpl() override;

 private:
  // |AuthProvider|
  void GetPersistentCredential(
      fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
      fidl::StringPtr user_profile_id,
      GetPersistentCredentialCallback callback) override;

  // |AuthProvider|
  void GetAppAccessToken(fidl::StringPtr credential,
                         fidl::StringPtr app_client_id,
                         const fidl::VectorPtr<fidl::StringPtr> app_scopes,
                         GetAppAccessTokenCallback callback) override;

  // |AuthProvider|
  void GetAppIdToken(fidl::StringPtr credential, fidl::StringPtr audience,
                     GetAppIdTokenCallback callback) override;

  // |AuthProvider|
  void GetAppFirebaseToken(fidl::StringPtr id_token,
                           fidl::StringPtr firebase_api_key,
                           GetAppFirebaseTokenCallback callback) override;

  // |AuthProvider|
  void RevokeAppOrPersistentCredential(
      fidl::StringPtr credential,
      RevokeAppOrPersistentCredentialCallback callback) override;

  // |AuthProvider|
  void GetPersistentCredentialFromAttestationJWT(
      fidl::InterfaceHandle<AttestationSigner> attestation_signer,
      AttestationJWTParams jwt_params,
      fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
      fidl::StringPtr user_profile_id,
      GetPersistentCredentialFromAttestationJWTCallback callback) override;

  // |AuthProvider|
  void GetAppAccessTokenFromAssertionJWT(
      fidl::InterfaceHandle<AttestationSigner> attestation_signer,
      AssertionJWTParams jwt_params, fidl::StringPtr credential,
      fidl::VectorPtr<::fidl::StringPtr> scopes,
      GetAppAccessTokenFromAssertionJWTCallback callback) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevAuthProviderImpl);
};

}  // namespace dev_auth_provider
}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_DEV_AUTH_PROVIDER_IMPL_H_
