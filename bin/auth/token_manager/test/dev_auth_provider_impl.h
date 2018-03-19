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
      f1dl::InterfaceHandle<auth::AuthenticationUIContext> auth_ui_context,
      const GetPersistentCredentialCallback& callback) override;

  // |AuthProvider|
  void GetAppAccessToken(const f1dl::StringPtr& credential,
                         const f1dl::StringPtr& app_client_id,
                         const f1dl::Array<f1dl::StringPtr> app_scopes,
                         const GetAppAccessTokenCallback& callback) override;

  // |AuthProvider|
  void GetAppIdToken(const f1dl::StringPtr& credential,
                     const f1dl::StringPtr& audience,
                     const GetAppIdTokenCallback& callback) override;

  // |AuthProvider|
  void GetAppFirebaseToken(
      const f1dl::StringPtr& id_token, const f1dl::StringPtr& firebase_api_key,
      const GetAppFirebaseTokenCallback& callback) override;

  // |AuthProvider|
  void RevokeAppOrPersistentCredential(
      const f1dl::StringPtr& credential,
      const RevokeAppOrPersistentCredentialCallback& callback) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevAuthProviderImpl);
};

}  // namespace dev_auth_provider
}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TEST_DEV_AUTH_PROVIDER_IMPL_H_
