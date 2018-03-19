// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "garnet/bin/auth/token_manager/token_manager_impl.h"
#include "garnet/public/lib/auth/fidl/auth_provider_factory.fidl.h"
#include "lib/app/cpp/connect.h"
#include "lib/svc/cpp/services.h"

namespace auth {

namespace {

const cache::CacheKey GetCacheKey(auth::AuthProviderType auth_provider_type,
                                  const f1dl::StringPtr& user_profile_id) {
  // TODO: consider replacing the static cast with a string map (more type safe)
  return cache::CacheKey(std::to_string(static_cast<int>(auth_provider_type)),
                         user_profile_id.get());
}

// Maps |AuthProviderType| to store |IdentityProvider| value.
auth::store::IdentityProvider MapToStoreIdentityProvider(
    auth::AuthProviderType provider_type) {
  switch (provider_type) {
    case AuthProviderType::GOOGLE:
      return auth::store::IdentityProvider::GOOGLE;
    case AuthProviderType::DEV:
      return auth::store::IdentityProvider::TEST;
  }
}
}  // namespace

using auth::AuthProviderStatus;
using auth::Status;

TokenManagerImpl::TokenManagerImpl(
    component::ApplicationContext* app_context,
    std::unique_ptr<store::AuthDb> auth_db,
    f1dl::VectorPtr<AuthProviderConfigPtr> auth_provider_configs)
    : token_cache_(kMaxCacheSize), auth_db_(std::move(auth_db)) {
  FXL_CHECK(app_context);
  // TODO: Start the auth provider only when someone does a request to it,
  // instead of starting all the configured providers in advance.
  for (auto& config : *auth_provider_configs) {
    if (config->url.get().empty()) {
      FXL_LOG(ERROR) << "Auth provider config url is not set.";
      continue;
    }

    auto launch_info = component::ApplicationLaunchInfo::New();
    launch_info->url = config->url;
    component::Services services;
    launch_info->directory_request = services.NewRequest();

    component::ApplicationControllerPtr controller;
    app_context->launcher()->CreateApplication(std::move(launch_info),
                                               controller.NewRequest());
    controller.set_error_handler([this, &config] {
      FXL_LOG(INFO) << "Auth provider " << config->url << " disconnected";
      auth_providers_.erase(config->auth_provider_type);
      auth_provider_controllers_.erase(config->auth_provider_type);
      // TODO: Try reconnecting to Auth provider using some back-off mechanism.
    });
    auth_provider_controllers_[config->auth_provider_type] =
        std::move(controller);

    auth::AuthProviderFactoryPtr auth_provider_factory;
    services.ConnectToService(auth_provider_factory.NewRequest());

    auth::AuthProviderPtr auth_provider_ptr;
    auth_provider_factory->GetAuthProvider(
        auth_provider_ptr.NewRequest(), [](auth::AuthProviderStatus status) {
          if (status != auth::AuthProviderStatus::OK) {
            FXL_LOG(ERROR) << "Failed to connect to the auth provider: "
                           << status;
          }
        });
    auth_provider_ptr.set_error_handler([this, &config] {
      FXL_LOG(INFO) << "Auth provider " << config->url << " disconnected";
      auth_providers_.erase(config->auth_provider_type);
      auth_provider_controllers_.erase(config->auth_provider_type);
      // TODO: Try reconnecting to Auth provider using some back-off mechanism.
    });
    auth_providers_[config->auth_provider_type] = std::move(auth_provider_ptr);
  }
}

TokenManagerImpl::~TokenManagerImpl() {}

void TokenManagerImpl::Authorize(
    const auth::AuthProviderType auth_provider_type,
    f1dl::InterfaceHandle<auth::AuthenticationUIContext> auth_ui_context,
    const AuthorizeCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  it->second->GetPersistentCredential(
      std::move(auth_ui_context),
      [this, auth_provider_type, callback](
          AuthProviderStatus status, f1dl::StringPtr credential,
          UserProfileInfoPtr user_profile_info) {
        if (status != AuthProviderStatus::OK || credential.get().empty()) {
          callback(Status::INTERNAL_ERROR, nullptr);
          return;
        }

        auto cred_id = store::CredentialIdentifier(
            user_profile_info->id,
            MapToStoreIdentityProvider(auth_provider_type));

        if (auth_db_->AddCredential(store::CredentialValue(
                cred_id, credential)) != store::Status::kOK) {
          // TODO: Log error
          callback(Status::INTERNAL_ERROR, nullptr);
        }

        callback(Status::OK, std::move(user_profile_info));
        return;
      });
}

void TokenManagerImpl::GetAccessToken(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::StringPtr& user_profile_id,
    const f1dl::StringPtr& app_client_id,
    f1dl::VectorPtr<f1dl::StringPtr> app_scopes,
    const GetAccessTokenCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  std::string credential;
  auto cred_id = store::CredentialIdentifier(
      user_profile_id, MapToStoreIdentityProvider(auth_provider_type));
  auth_db_->GetRefreshToken(cred_id, &credential);

  auto cache_key = GetCacheKey(auth_provider_type, user_profile_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK &&
      tokens.access_token.IsValid()) {
    callback(Status::OK, tokens.access_token.token);
    return;
  }

  it->second->GetAppAccessToken(
      f1dl::StringPtr(credential), app_client_id, std::move(app_scopes),
      [this, callback, cache_key, &tokens](AuthProviderStatus status,
                                           AuthTokenPtr access_token) {
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

void TokenManagerImpl::GetIdToken(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::StringPtr& user_profile_id,
    const f1dl::StringPtr& audience,
    const GetIdTokenCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  std::string credential;
  auto cred_id = store::CredentialIdentifier(
      user_profile_id, MapToStoreIdentityProvider(auth_provider_type));
  auth_db_->GetRefreshToken(cred_id, &credential);
  auto cache_key = GetCacheKey(auth_provider_type, user_profile_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK &&
      tokens.id_token.IsValid()) {
    callback(Status::OK, tokens.id_token.token);
    return;
  }

  it->second->GetAppIdToken(
      f1dl::StringPtr(credential), audience,
      [this, callback, cache_key, &tokens](AuthProviderStatus status,
                                           AuthTokenPtr id_token) {
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

void TokenManagerImpl::GetFirebaseToken(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::StringPtr& user_profile_id,
    const f1dl::StringPtr& audience,
    const f1dl::StringPtr& firebase_api_key,
    const GetFirebaseTokenCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  auto cache_key = GetCacheKey(auth_provider_type, user_profile_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK) {
    auto fb_token_it = tokens.firebase_tokens_map.find(firebase_api_key);
    if (fb_token_it != tokens.firebase_tokens_map.end()) {
      auto fb_token = auth::FirebaseToken::New();
      fb_token->id_token = fb_token_it->second.fb_id_token;
      fb_token->email = fb_token_it->second.email;
      fb_token->local_id = fb_token_it->second.local_id;

      callback(Status::OK, std::move(fb_token));
      return;
    }
  }

  GetIdToken(auth_provider_type, user_profile_id, audience,
             [this, it, callback, cache_key, firebase_api_key](
                 Status status, f1dl::StringPtr id_token) {
               if (status != Status::OK) {
                 callback(Status::AUTH_PROVIDER_SERVER_ERROR, nullptr);
                 // TODO: log error here
                 return;
               }

               it->second->GetAppFirebaseToken(
                   id_token, firebase_api_key,
                   [this, callback, cache_key, firebase_api_key](
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

                     if (token_cache_.AddFirebaseToken(
                             cache_key, firebase_api_key,
                             std::move(cached_token)) != cache::Status::kOK) {
                       callback(Status::OK, std::move(fb_token));
                       // TODO: log error
                       return;
                     }

                     callback(Status::OK, std::move(fb_token));
                   });
             });
}

void TokenManagerImpl::DeleteAllTokens(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::StringPtr& user_profile_id,
    const DeleteAllTokensCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE);
  }

  std::string credential;
  auto cred_id = store::CredentialIdentifier(
      user_profile_id, MapToStoreIdentityProvider(auth_provider_type));
  cache::CacheKey cache_key = GetCacheKey(auth_provider_type, user_profile_id);

  auth_db_->GetRefreshToken(cred_id, &credential);

  it->second->RevokeAppOrPersistentCredential(
      f1dl::StringPtr(credential), [this, auth_provider_type, user_profile_id,
                                 callback](AuthProviderStatus status) {
        if (status != AuthProviderStatus::OK) {
          callback(Status::AUTH_PROVIDER_SERVER_ERROR);
          return;
        }

        auto cache_status = token_cache_.Delete(
            GetCacheKey(auth_provider_type, user_profile_id));
        if (cache_status != cache::Status::kOK &&
            cache_status != cache::Status::kKeyNotFound) {
          callback(Status::INTERNAL_CACHE_ERROR);
          return;
        }

        auto cred_id = store::CredentialIdentifier(
            user_profile_id, MapToStoreIdentityProvider(auth_provider_type));
        auth_db_->DeleteCredential(cred_id);

        callback(Status::OK);
      });
}

}  // namespace auth
