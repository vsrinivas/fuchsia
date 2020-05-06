// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_USER_PROVIDER_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_USER_PROVIDER_IMPL_H_

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/modular/lib/async/cpp/future.h"

namespace modular {

// This class manages the session-to-persona mapping (which personas are
// participating in which sessions).
//
// The current policy is to automatically login every newly added account's
// default persona into a new session. Whether a new session gets started or not
// is up to |session_provider_impl|.
class SessionUserProviderImpl : fuchsia::modular::UserProvider {
 public:
  // Called after SessionUserProviderImpl successfully logs in a user.
  using OnLoginCallback = fit::function<void(bool is_ephemeral_account)>;

  // |on_login| Callback invoked when a persona is ready to be logged into a
  // new session. Must be present.
  SessionUserProviderImpl(OnLoginCallback on_login);

  void Connect(fidl::InterfaceRequest<fuchsia::modular::UserProvider> request);

  // |fuchsia::modular::UserProvider|, also called by |basemgr_impl|.
  void Login3(bool is_ephemeral_account) override;

  void RemoveAllUsers(fit::function<void()> callback);

 private:
  // |fuchsia::modular::UserProvider|
  void Login(fuchsia::modular::UserLoginParams params) override;

  // |fuchsia::modular::UserProvider|
  void Login2(fuchsia::modular::UserLoginParams2 params) override;

  // |fuchsia::modular::UserProvider|
  void AddUser(fuchsia::modular::auth::IdentityProvider identity_provider,
               AddUserCallback callback) override;

  // |fuchsia::modular::UserProvider|
  void RemoveUser(std::string account_id, RemoveUserCallback callback) override;

  // |fuchsia::modular::UserProvider|
  void PreviousUsers(PreviousUsersCallback callback) override;

  fidl::BindingSet<fuchsia::modular::UserProvider> bindings_;

  OnLoginCallback on_login_;

  fxl::WeakPtrFactory<SessionUserProviderImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionUserProviderImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_USER_PROVIDER_IMPL_H_
