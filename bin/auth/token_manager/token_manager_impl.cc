// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "garnet/bin/auth/token_manager/token_manager_impl.h"
#include "garnet/public/lib/auth/fidl/auth_provider_factory.fidl.h"
#include "lib/app/cpp/connect.h"
#include "lib/svc/cpp/services.h"

namespace auth {

using auth::AuthProviderStatus;
using auth::Status;

TokenManagerImpl::TokenManagerImpl(
    app::ApplicationContext* app_context,
    f1dl::Array<AuthProviderConfigPtr> auth_provider_configs)
    : token_cache_(kMaxCacheSize) {
  FXL_CHECK(app_context);

  // TODO: Start the auth provider only when someone does a request to it,
  // instead of starting all the configured providers in advance.
  for (auto& config : auth_provider_configs) {
    if (config->url.get().empty()) {
      FXL_LOG(ERROR) << "Auth provider config url is not set.";
      continue;
    }

    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = config->url;
    app::Services services;
    launch_info->service_request = services.NewRequest();

    app::ApplicationControllerPtr controller;
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
      [this, callback](AuthProviderStatus status, f1dl::String credential) {
        if (status != AuthProviderStatus::OK || credential.get().empty()) {
          callback(Status::INTERNAL_ERROR, nullptr);
          return;
        }

        // TODO: Save credential to data store
        callback(Status::OK, nullptr);
        return;
      });
}

void TokenManagerImpl::GetAccessToken(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::String& app_client_id,
    f1dl::Array<f1dl::String> app_scopes,
    const GetAccessTokenCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  // TODO: Fetch credential from data store
  f1dl::String credential = "TODO";
  f1dl::String idp_credential_id = "TODO";

  auto cache_key = GetCacheKey(auth_provider_type, idp_credential_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK &&
      tokens.access_token.IsValid()) {
    callback(Status::OK, tokens.access_token.token);
    return;
  }

  it->second->GetAppAccessToken(
      idp_credential_id, app_client_id, std::move(app_scopes),
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
        return;
      });
}

void TokenManagerImpl::GetIdToken(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::String& audience,
    const GetIdTokenCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  // TODO: Fetch credential from data store
  f1dl::String credential = "TODO";
  f1dl::String idp_credential_id = "TODO";

  auto cache_key = GetCacheKey(auth_provider_type, idp_credential_id);
  cache::OAuthTokens tokens;

  if (token_cache_.Get(cache_key, &tokens) == cache::Status::kOK &&
      tokens.id_token.IsValid()) {
    callback(Status::OK, tokens.id_token.token);
    return;
  }

  it->second->GetAppIdToken(
      credential, audience,
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
        return;
      });
}

void TokenManagerImpl::GetFirebaseToken(
    const auth::AuthProviderType auth_provider_type,
    const f1dl::String& audience,
    const f1dl::String& firebase_api_key,
    const GetFirebaseTokenCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE, nullptr);
  }

  f1dl::String idp_credential_id = "TODO";

  auto cache_key = GetCacheKey(auth_provider_type, idp_credential_id);
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

  GetIdToken(auth_provider_type, audience,
             [this, it, callback, cache_key, firebase_api_key](
                 Status status, f1dl::String id_token) {
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
                     return;
                   });
             });
}

void TokenManagerImpl::DeleteAllTokens(
    const auth::AuthProviderType auth_provider_type,
    const DeleteAllTokensCallback& callback) {
  auto it = auth_providers_.find(auth_provider_type);
  if (it == auth_providers_.end()) {
    callback(Status::AUTH_PROVIDER_SERVICE_UNAVAILABLE);
  }

  // TODO: Fetch credential from data store
  f1dl::String credential = "TODO";
  f1dl::String idp_credential_id = "TODO";
  cache::CacheKey cache_key =
      GetCacheKey(auth_provider_type, idp_credential_id);

  it->second->RevokeAppOrPersistentCredential(
      credential, [this, cache_key, callback](AuthProviderStatus status) {
        if (status != AuthProviderStatus::OK) {
          callback(Status::AUTH_PROVIDER_SERVER_ERROR);
          return;
        }

        auto cache_status = token_cache_.Delete(cache_key);
        if (cache_status != cache::Status::kOK &&
            cache_status != cache::Status::kKeyNotFound) {
          callback(Status::INTERNAL_CACHE_ERROR);
          return;
        }

        //  TODO: Delete local copy from data store
        callback(Status::OK);
        return;
      });
}

const cache::CacheKey TokenManagerImpl::GetCacheKey(
    auth::AuthProviderType identity_provider,
    const f1dl::String& idp_credential_id) {
  // TODO: consider replacing the static cast with a string map (more type safe)
  return cache::CacheKey(std::to_string(static_cast<int>(identity_provider)),
                         idp_credential_id.get());
}

}  // namespace auth
