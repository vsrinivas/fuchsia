// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>

#include "garnet/bin/auth/token_manager/token_manager_impl.h"
#include "lib/component/cpp/connect.h"
#include "lib/svc/cpp/services.h"

namespace auth {

namespace {

const cache::CacheKey GetCacheKey(fidl::StringPtr auth_provider_type,
                                  fidl::StringPtr user_profile_id) {
  return cache::CacheKey(auth_provider_type, user_profile_id.get());
}

fuchsia::auth::Status MapStoreStatus(store::Status status) {
  switch (status) {
    case store::Status::kOK:
      return fuchsia::auth::Status::OK;
    case store::Status::kInvalidArguments:
      return fuchsia::auth::Status::INVALID_REQUEST;
    case store::Status::kOperationFailed:
      return fuchsia::auth::Status::IO_ERROR;
    case store::Status::kDbNotInitialized:
      return fuchsia::auth::Status::INTERNAL_ERROR;
    case store::Status::kCredentialNotFound:
      return fuchsia::auth::Status::USER_NOT_FOUND;
    default:
      return fuchsia::auth::Status::UNKNOWN_ERROR;
  }
}

}  // namespace

using fuchsia::auth::AppConfig;
using fuchsia::auth::AuthProviderStatus;
using fuchsia::auth::AuthTokenPtr;
using fuchsia::auth::FirebaseTokenPtr;
using fuchsia::auth::Status;

TokenManagerImpl::TokenManagerImpl(
    component::StartupContext* app_context,
    std::unique_ptr<store::AuthDb> auth_db,
    fidl::VectorPtr<fuchsia::auth::AuthProviderConfig> auth_provider_configs,
    fidl::InterfaceHandle<fuchsia::auth::AuthenticationContextProvider>
        auth_context_provider)
    : token_cache_(kMaxCacheSize), auth_db_(std::move(auth_db)) {
  FXL_CHECK(app_context);

  // Initialize the UI display context for each token manager instance.
  auth_context_provider_.Bind(std::move(auth_context_provider));

  // TODO: Start the auth provider only when someone does a request to it,
  // instead of starting all the configured providers in advance.
  for (auto& config : *auth_provider_configs) {
    if (config.url.get().empty()) {
      FXL_LOG(ERROR) << "Auth provider config url is not set.";
      continue;
    }

    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = config.url;
    component::Services services;
    launch_info.directory_request = services.NewRequest();

    fuchsia::sys::ComponentControllerPtr controller;
    app_context->launcher()->CreateComponent(std::move(launch_info),
                                             controller.NewRequest());
    controller.set_error_handler(
        [this, url = config.url,
         auth_provider_type = config.auth_provider_type] {
          FXL_LOG(INFO) << "Auth provider " << url << " disconnected";
          auth_providers_.erase(auth_provider_type);
          auth_provider_controllers_.erase(auth_provider_type);
          // TODO: Try reconnecting to Auth provider using some back-off
          // mechanism.
        });
    auth_provider_controllers_[config.auth_provider_type] =
        std::move(controller);

    fuchsia::auth::AuthProviderFactoryPtr auth_provider_factory;
    services.ConnectToService(auth_provider_factory.NewRequest());

    fuchsia::auth::AuthProviderPtr auth_provider_ptr;
    auth_provider_factory->GetAuthProvider(
        auth_provider_ptr.NewRequest(), [](auth::AuthProviderStatus status) {
          if (status != auth::AuthProviderStatus::OK) {
            FXL_LOG(ERROR) << "Failed to connect to the auth provider: "
                           << static_cast<uint32_t>(status);
          }
        });
    auth_provider_ptr.set_error_handler(
        [this, url = config.url,
         auth_provider_type = config.auth_provider_type] {
          FXL_LOG(INFO) << "Auth provider " << url << " disconnected";
          auth_providers_.erase(auth_provider_type);
          auth_provider_controllers_.erase(auth_provider_type);
          // TODO: Try reconnecting to Auth provider using some back-off
          // mechanism.
        });
    auth_providers_[config.auth_provider_type] = std::move(auth_provider_ptr);
  }
}

TokenManagerImpl::~TokenManagerImpl() {}

void TokenManagerImpl::Authorize(AppConfig app_config,
                                 fidl::VectorPtr<fidl::StringPtr> app_scopes,
                                 fidl::StringPtr user_profile_id,
                                 AuthorizeCallback callback) {
  auto it = auth_providers_.find(app_config.auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
    return;
  }

  fuchsia::auth::AuthenticationUIContextPtr auth_ui_context;
  auth_context_provider_->GetAuthenticationUIContext(
      auth_ui_context.NewRequest());

  auth_ui_context.set_error_handler([this, callback = callback.share()] {
    FXL_LOG(INFO) << "Auth UI Context disconnected";
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  });

  it->second->GetPersistentCredential(
      std::move(auth_ui_context), fidl::StringPtr(user_profile_id),
      [this, auth_provider_type = app_config.auth_provider_type,
       callback = std::move(callback)](
          AuthProviderStatus status, fidl::StringPtr credential,
          fuchsia::auth::UserProfileInfoPtr user_profile_info) {
        if (status != AuthProviderStatus::OK || credential.get().empty()) {
          callback(Status::INTERNAL_ERROR, nullptr);
          return;
        }

        auto cred_id = store::CredentialIdentifier(user_profile_info->id,
                                                   auth_provider_type);

        if (auth_db_->AddCredential(store::CredentialValue(
                cred_id, credential)) != store::Status::kOK) {
          // TODO: Log error
          callback(Status::INTERNAL_ERROR, nullptr);
          return;
        }

        callback(Status::OK, std::move(user_profile_info));
        return;
      });
}

void TokenManagerImpl::GetAccessToken(
    AppConfig app_config, fidl::StringPtr user_profile_id,
    fidl::VectorPtr<fidl::StringPtr> app_scopes,
    GetAccessTokenCallback callback) {
  auto it = auth_providers_.find(app_config.auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
    return;
  }

  std::string credential;
  auto cred_id = store::CredentialIdentifier(user_profile_id,
                                             app_config.auth_provider_type);
  auto credential_status = auth_db_->GetRefreshToken(cred_id, &credential);
  if (credential_status != store::Status::kOK) {
    callback(MapStoreStatus(credential_status), nullptr);
    return;
  }

  auto cache_key = GetCacheKey(app_config.auth_provider_type, user_profile_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK &&
      tokens.access_token.IsValid()) {
    callback(Status::OK, tokens.access_token.token);
    return;
  }

  it->second->GetAppAccessToken(
      fidl::StringPtr(credential), app_config.client_id, std::move(app_scopes),
      [this, callback = std::move(callback), cache_key, tokens](
          AuthProviderStatus status, AuthTokenPtr access_token) mutable {
        std::string access_token_val;
        if (access_token) {
          access_token_val = access_token->token;
        }

        if (status != AuthProviderStatus::OK) {
          callback(Status::AUTH_PROVIDER_SERVER_ERROR, access_token_val);
          return;
        }

        tokens.access_token.expiration_time =
            fxl::TimePoint::Now() +
            fxl::TimeDelta::FromSeconds(access_token->expires_in);
        tokens.access_token.token = access_token_val;

        auto cache_status = token_cache_.Put(cache_key, tokens);
        if (cache_status != cache::Status::kOK) {
          // TODO: log error
          callback(Status::OK, access_token_val);
          return;
        }

        callback(Status::OK, std::move(access_token_val));
      });
}

void TokenManagerImpl::GetIdToken(AppConfig app_config,
                                  fidl::StringPtr user_profile_id,
                                  fidl::StringPtr audience,
                                  GetIdTokenCallback callback) {
  auto it = auth_providers_.find(app_config.auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
    return;
  }

  std::string credential;
  auto cred_id = store::CredentialIdentifier(user_profile_id,
                                             app_config.auth_provider_type);
  auto credential_status = auth_db_->GetRefreshToken(cred_id, &credential);
  if (credential_status != store::Status::kOK) {
    callback(MapStoreStatus(credential_status), nullptr);
    return;
  }

  auto cache_key = GetCacheKey(app_config.auth_provider_type, user_profile_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK &&
      tokens.id_token.IsValid()) {
    callback(Status::OK, tokens.id_token.token);
    return;
  }

  it->second->GetAppIdToken(
      fidl::StringPtr(credential), audience,
      [this, callback = std::move(callback), cache_key, tokens](
          AuthProviderStatus status, AuthTokenPtr id_token) mutable {
        std::string id_token_val;
        if (id_token) {
          id_token_val = id_token->token;
        }

        if (status != AuthProviderStatus::OK) {
          callback(Status::AUTH_PROVIDER_SERVER_ERROR, id_token_val);
          return;
        }

        tokens.id_token.expiration_time =
            fxl::TimePoint::Now() +
            fxl::TimeDelta::FromSeconds(id_token->expires_in);
        tokens.id_token.token = id_token_val;

        auto cache_status = token_cache_.Put(cache_key, tokens);
        if (cache_status != cache::Status::kOK) {
          // TODO: log error
          callback(Status::OK, id_token_val);
          return;
        }

        callback(Status::OK, id_token_val);
      });
}

void TokenManagerImpl::GetFirebaseToken(AppConfig app_config,
                                        fidl::StringPtr user_profile_id,
                                        fidl::StringPtr audience,
                                        fidl::StringPtr firebase_api_key,
                                        GetFirebaseTokenCallback callback) {
  auto it = auth_providers_.find(app_config.auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
    return;
  }

  auto cache_key = GetCacheKey(app_config.auth_provider_type, user_profile_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK) {
    auto fb_token_it = tokens.firebase_tokens_map.find(firebase_api_key);
    if (fb_token_it != tokens.firebase_tokens_map.end()) {
      auto fb_token = fuchsia::auth::FirebaseToken::New();
      fb_token->id_token = fb_token_it->second.fb_id_token;
      fb_token->email = fb_token_it->second.email;
      fb_token->local_id = fb_token_it->second.local_id;

      callback(Status::OK, std::move(fb_token));
      return;
    }
  }

  GetIdToken(
      std::move(app_config), user_profile_id, audience,
      [this, it, callback = std::move(callback), cache_key, firebase_api_key](
          Status status, fidl::StringPtr id_token) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          // TODO: log error here
          return;
        }

        it->second->GetAppFirebaseToken(
            id_token, firebase_api_key,
            [this, callback = std::move(callback), cache_key, firebase_api_key](
                AuthProviderStatus status, FirebaseTokenPtr fb_token) {
              if (status != AuthProviderStatus::OK) {
                callback(Status::AUTH_PROVIDER_SERVER_ERROR, nullptr);
                return;
              }

              cache::FirebaseAuthToken cached_token;
              cached_token.fb_id_token = fb_token->id_token;
              cached_token.expiration_time =
                  fxl::TimePoint::Now() +
                  fxl::TimeDelta::FromSeconds(fb_token->expires_in);
              cached_token.local_id = fb_token->local_id;
              cached_token.email = fb_token->email;

              if (token_cache_.AddFirebaseToken(cache_key, firebase_api_key,
                                                std::move(cached_token)) !=
                  cache::Status::kOK) {
                callback(Status::OK, std::move(fb_token));
                // TODO: log error
                return;
              }

              callback(Status::OK, std::move(fb_token));
            });
      });
}

void TokenManagerImpl::DeleteAllTokens(AppConfig app_config,
                                       fidl::StringPtr user_profile_id,
                                       DeleteAllTokensCallback callback) {
  auto it = auth_providers_.find(app_config.auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE);
  }

  std::string credential;
  auto cred_id = store::CredentialIdentifier(user_profile_id,
                                             app_config.auth_provider_type);
  cache::CacheKey cache_key =
      GetCacheKey(app_config.auth_provider_type, user_profile_id);

  auth_db_->GetRefreshToken(cred_id, &credential);

  it->second->RevokeAppOrPersistentCredential(
      fidl::StringPtr(credential),
      [this, app_config, user_profile_id,
       callback = std::move(callback)](AuthProviderStatus status) {
        if (status != AuthProviderStatus::OK) {
          callback(Status::AUTH_PROVIDER_SERVER_ERROR);
          return;
        }

        auto cache_status = token_cache_.Delete(
            GetCacheKey(app_config.auth_provider_type, user_profile_id));
        if (cache_status != cache::Status::kOK &&
            cache_status != cache::Status::kKeyNotFound) {
          callback(Status::INTERNAL_ERROR);
          return;
        }

        auto cred_id = store::CredentialIdentifier(
            user_profile_id, app_config.auth_provider_type);
        auth_db_->DeleteCredential(cred_id);

        callback(Status::OK);
      });
}

}  // namespace auth
