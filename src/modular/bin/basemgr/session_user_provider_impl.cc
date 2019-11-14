// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_user_provider_impl.h"

#include <lib/fit/function.h>
#include <zircon/status.h>

namespace modular {

namespace {

// Url of the application launching token manager
constexpr char kSessionUserProviderAppUrl[] = "session_user_provider_url";

// Dev auth provider configuration
constexpr char kDevAuthProviderType[] = "dev";
constexpr char kDevAuthProviderUrl[] =
    "fuchsia-pkg://fuchsia.com/dev_auth_provider#meta/"
    "dev_auth_provider.cmx";

// Google auth provider configuration
constexpr char kGoogleAuthProviderType[] = "google";
constexpr char kGoogleAuthProviderUrl[] =
    "fuchsia-pkg://fuchsia.com/google_auth_provider#meta/"
    "google_auth_provider.cmx";

std::string GetRandomId() {
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  return std::to_string(random_number);
}

// Returns the corresponding |auth_provider_type| string that maps to
// |fuchsia::modular::auth::IdentityProvider| value.
// TODO(ukode): Convert enum |fuchsia::modular::auth::IdentityProvider| to
// fidl::String datatype to make it consistent in the future.
std::string MapIdentityProviderToAuthProviderType(
    const fuchsia::modular::auth::IdentityProvider idp) {
  switch (idp) {
    case fuchsia::modular::auth::IdentityProvider::DEV:
      return kDevAuthProviderType;
    case fuchsia::modular::auth::IdentityProvider::GOOGLE:
      return kGoogleAuthProviderType;
  }
  FXL_DCHECK(false) << "Unrecognized IDP.";
}

// Returns a list of supported auth provider configurations that includes the
// type, startup parameters and the url of the auth provider component.
// TODO(ukode): This list will be derived from a config package in the future.
std::vector<fuchsia::auth::AuthProviderConfig> GetAuthProviderConfigs() {
  fuchsia::auth::AuthProviderConfig dev_auth_provider_config;
  dev_auth_provider_config.auth_provider_type = kDevAuthProviderType;
  dev_auth_provider_config.url = kDevAuthProviderUrl;

  fuchsia::auth::AuthProviderConfig google_auth_provider_config;
  google_auth_provider_config.auth_provider_type = kGoogleAuthProviderType;
  google_auth_provider_config.url = kGoogleAuthProviderUrl;

  std::vector<fuchsia::auth::AuthProviderConfig> auth_provider_configs;
  auth_provider_configs.push_back(std::move(google_auth_provider_config));
  auth_provider_configs.push_back(std::move(dev_auth_provider_config));

  return auth_provider_configs;
}

}  // namespace

SessionUserProviderImpl::SessionUserProviderImpl(
    fuchsia::identity::account::AccountManager* const account_manager,
    fuchsia::auth::TokenManagerFactory* const token_manager_factory,
    fuchsia::auth::AuthenticationContextProviderPtr authentication_context_provider,
    OnInitializeCallback on_initialize, OnLoginCallback on_login)
    : account_manager_(account_manager),
      token_manager_factory_(token_manager_factory),
      authentication_context_provider_(std::move(authentication_context_provider)),
      authentication_context_provider_binding_(this),
      account_listener_binding_(this),
      on_initialize_(std::move(on_initialize)),
      on_login_(std::move(on_login)),
      weak_factory_(this) {
  FXL_CHECK(account_manager_);
  FXL_CHECK(token_manager_factory_);
  FXL_CHECK(authentication_context_provider_);
  FXL_CHECK(on_initialize_);
  FXL_CHECK(on_login_);

  authentication_context_provider_binding_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(WARNING) << "AuthenticationContextProvider disconnected.";
    authentication_context_provider_binding_.Unbind();
  });

  // Register SessionUserProvider as an AccountListener. All added accounts will
  // be logged in to a session.
  account_listener_binding_.set_error_handler([](zx_status_t status) {
    FXL_LOG(FATAL) << "AccountListener disconnected with status: " << zx_status_get_string(status);
  });

  fuchsia::identity::account::AccountListenerOptions options;
  options.initial_state = true;
  options.add_account = true;
  account_manager_->RegisterAccountListener(
      account_listener_binding_.NewBinding(), std::move(options),
      [weak_this = weak_factory_.GetWeakPtr()](auto result) {
        if (!weak_this) {
          return;
        }
        if (result.is_response()) {
          FXL_LOG(INFO) << "AccountListener registered.";
        } else {
          FXL_LOG(FATAL) << "AccountListener registration failed with status: "
                         << (uint32_t)result.err();
        }
      });
}

void SessionUserProviderImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SessionUserProviderImpl::AddUser(fuchsia::modular::auth::IdentityProvider identity_provider,
                                      AddUserCallback callback) {
  account_manager_->ProvisionFromAuthProvider(
      authentication_context_provider_binding_.NewBinding(),
      MapIdentityProviderToAuthProviderType(identity_provider),
      fuchsia::identity::account::Lifetime::PERSISTENT,
      [weak_this = weak_factory_.GetWeakPtr(),
       callback = std::move(callback)](auto result) mutable {
        if (weak_this) {
          return;
        }
        if (result.is_err()) {
          FXL_LOG(ERROR) << "Failed to provision new account from with "
                            "provider with error: "
                         << (uint32_t)result.err();
          callback(nullptr, "Failed to provision new account from auth provider.");
          return;
        }
        auto response = result.response();

        // To interface with BaseShells that haven't migrated to use
        // AccountManager, we give them the string account_id to call back
        // Login with.
        auto account = fuchsia::modular::auth::Account::New();
        account->id = std::to_string(response.account_id);

        callback(std::move(account), "");
      });
}

void SessionUserProviderImpl::Login(fuchsia::modular::UserLoginParams params) {
  Login2(fuchsia::modular::UserLoginParams2{
      .account_id = std::move(params.account_id),
  });
}

void SessionUserProviderImpl::Login2(fuchsia::modular::UserLoginParams2 params) {
  bool login_as_guest = params.account_id.value_or("") == "";
  if (login_as_guest) {
    FXL_LOG(INFO) << "fuchsia::modular::UserProvider::Login() Login as guest";
    // If requested, login as guest  by creating token managers with token
    // manager factory.
    auto account_id = GetRandomId();

    // Instead of passing token_manager_factory all the way to agents and
    // runners with all auth provider configurations, send two
    // |fuchsia::auth::TokenManager| handles, one for ledger and one for
    // agents for the given user account |account_id|.
    fuchsia::auth::TokenManagerPtr ledger_token_manager = CreateTokenManager(account_id);
    fuchsia::auth::TokenManagerPtr agent_token_manager = CreateTokenManager(account_id);

    on_login_(/* account= */ nullptr, std::move(ledger_token_manager),
              std::move(agent_token_manager));
  } else {
    FXL_LOG(INFO) << "fuchsia::modular::UserProvider::Login() Login as "
                     "authenticated user";
    uint64_t account_id = std::atoll(params.account_id->c_str());

    fuchsia::identity::account::AccountPtr account;
    account_manager_->GetAccount(
        account_id, authentication_context_provider_binding_.NewBinding(), account.NewRequest(),
        [](auto result) { FXL_LOG(INFO) << "Got account with error: " << (uint32_t)result.err(); });

    fuchsia::identity::account::PersonaPtr persona;
    account->GetDefaultPersona(persona.NewRequest(), [](auto result) {
      FXL_LOG(INFO) << "Got default persona with error: " << (uint32_t)result.err();
    });

    fuchsia::auth::TokenManagerPtr ledger_token_manager;
    persona->GetTokenManager(
        kSessionUserProviderAppUrl, ledger_token_manager.NewRequest(), [](auto result) {
          FXL_LOG(INFO) << "Got token manager with error: " << (uint32_t)result.err();
        });

    fuchsia::auth::TokenManagerPtr agent_token_manager;
    persona->GetTokenManager(
        kSessionUserProviderAppUrl, agent_token_manager.NewRequest(), [](auto result) {
          FXL_LOG(INFO) << "Got token manager with error: " << (uint32_t)result.err();
        });

    auto account_deprecated = fuchsia::modular::auth::Account::New();
    account_deprecated->id = params.account_id->c_str();

    // Save the newly added user as a joined persona.
    struct JoinedPersona joined_persona {
      .account = std::move(account), .persona = std::move(persona),
    };
    joined_personas_.emplace_back(std::move(joined_persona));

    on_login_(std::move(account_deprecated), std::move(ledger_token_manager),
              std::move(agent_token_manager));
  }
}

void SessionUserProviderImpl::RemoveAllUsers(fit::function<void()> callback) {
  joined_personas_.clear();
  account_manager_->GetAccountIds(
      [weak_this = weak_factory_.GetWeakPtr(),
       callback = std::move(callback)](std::vector<uint64_t> account_ids) mutable {
        if (!weak_this) {
          return;
        }
        if (account_ids.empty()) {
          callback();
          return;
        }

        // We only expect there to be one account at most.
        weak_this->account_manager_->RemoveAccount(
            account_ids.at(0), true, /* Force account removal */
            [callback = std::move(callback)](auto) { callback(); });
      });
}

void SessionUserProviderImpl::RemoveUser(std::string account_id, RemoveUserCallback callback) {
  FXL_LOG(INFO) << "RemoveUser() is not implemented yet.";
  callback("");
}

void SessionUserProviderImpl::PreviousUsers(PreviousUsersCallback callback) {
  FXL_LOG(INFO) << "PreviousUsers() is not implemented yet";
  std::vector<::fuchsia::modular::auth::Account> users;
  callback(std::move(users));
}

void SessionUserProviderImpl::GetAuthenticationUIContext(
    fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) {
  authentication_context_provider_->GetAuthenticationUIContext(std::move(request));
}

fuchsia::auth::TokenManagerPtr SessionUserProviderImpl::CreateTokenManager(std::string account_id) {
  FXL_CHECK(token_manager_factory_);

  fuchsia::auth::TokenManagerPtr token_mgr;
  token_manager_factory_->GetTokenManager(
      account_id, kSessionUserProviderAppUrl, GetAuthProviderConfigs(),
      authentication_context_provider_binding_.NewBinding(), token_mgr.NewRequest());

  token_mgr.set_error_handler([account_id](zx_status_t status) {
    FXL_LOG(INFO) << "Token Manager for account:" << account_id << " disconnected";
  });

  return token_mgr;
}

void SessionUserProviderImpl::OnInitialize(
    std::vector<fuchsia::identity::account::InitialAccountState>, OnInitializeCallback callback) {
  callback();
  on_initialize_();
}

void SessionUserProviderImpl::OnAccountAdded(
    fuchsia::identity::account::InitialAccountState account_state,
    OnAccountAddedCallback callback) {
  // TODO(MF-311): Get rid of this once clients of UserProvider interface start
  // using AccountManager.
  fuchsia::modular::UserLoginParams params;
  params.account_id = std::to_string(account_state.account_id);
  // Base shell may also call Login with the newly added account, but the Login
  // flow should be resilient to multiple invocations.
  Login(std::move(params));
  callback();
}

void SessionUserProviderImpl::OnAccountRemoved(uint64_t, OnAccountRemovedCallback callback) {
  callback();
}

void SessionUserProviderImpl::OnAuthStateChanged(fuchsia::identity::account::AccountAuthState,
                                                 OnAuthStateChangedCallback callback) {
  callback();
}

}  // namespace modular
