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

#include "lib/auth/fidl/auth_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

namespace auth {
namespace dev_auth_provider {

class DevAuthProviderImpl : public auth::AuthProvider {
 public:
  DevAuthProviderImpl();

  ~DevAuthProviderImpl() override;

 private:
  // |AuthProvider|
  void GetPersistentCredential(
      fidl::InterfaceHandle<auth::AuthenticationUIContext> auth_ui_context,
      const GetPersistentCredentialCallback& callback) override;

  // |AuthProvider|
  void GetAppAccessToken(const fidl::String& credential,
                         const fidl::String& app_client_id,
                         const fidl::Array<fidl::String> app_scopes,
                         const GetAppAccessTokenCallback& callback) override;

  // |AuthProvider|
  void GetAppIdToken(const fidl::String& credential,
                     const fidl::String& audience,
                     const GetAppIdTokenCallback& callback) override;

  // |AuthProvider|
  void GetAppFirebaseToken(
      const fidl::String& id_token, const fidl::String& firebase_api_key,
      const GetAppFirebaseTokenCallback& callback) override;

  // |AuthProvider|
  void RevokeAppOrPersistentCredential(
      const fidl::String& credential,
      const RevokeAppOrPersistentCredentialCallback& callback) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevAuthProviderImpl);
};

}  // namespace dev_auth_provider
}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_DEV_AUTH_PROVIDER_IMPL_H_
