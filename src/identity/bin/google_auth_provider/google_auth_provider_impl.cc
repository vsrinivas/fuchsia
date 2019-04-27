// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/bin/google_auth_provider/google_auth_provider_impl.h"

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/vfs/cpp/service.h>

#include "garnet/public/lib/rapidjson_utils/rapidjson_validation.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "src/identity/bin/google_auth_provider/constants.h"
#include "src/identity/lib/oauth/oauth_request_builder.h"
#include "src/identity/lib/oauth/oauth_response.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace google_auth_provider {

namespace http = ::fuchsia::net::oldhttp;

namespace {

constexpr char kInjectionEntry[] = "LegacyAuthCredentialInjector";

std::string GetClientId(const std::string& app_client_id) {
  // By default, use the client_id of the invoking application.
  std::string client_id = app_client_id;

  // Use hard-coded Fuchsia client_id for downscoped tokens, if |app_client_id|
  // is missing.
  if (app_client_id.empty()) {
    client_id = kFuchsiaClientId;
  }

  return client_id;
}

// Outputs information from a failing OAuthResponse to the syslog.
void LogOauthResponse(const char* operation,
                      const auth_providers::oauth::OAuthResponse& response) {
  FX_LOGF(WARNING, NULL,
          "OAuthResponse error during %s: %s (Full response: %s)", operation,
          response.error_description.c_str(),
          modular::JsonValueToPrettyString(response.json_response).c_str());
}

// Sometimes auth codes contain non alpha characters such as a slash. When we
// receive these in a url parameter they are Hex encoded, but they need to be
// translated back to UTF-8 before using the auth code.
//
// TODO(jsankey): Remove this once we migrate to cookie delivery, or use a
// common encoding/decoding library if that arrives earlier.
void NormalizeAuthCode(std::string* code) {
  // This function uses the following literals:
  //   1 - The length of '%'
  //   2 - The length of a hex byte, e.g. '2F'
  //   3 - The length of a %-prefixed hex byte, e.g. '%2F'
  //  16 - The base of hexadecimal
  //  32 - The smallest printable character, i.e. the space character
  // 127 - The largest single byte UTF-8 codepoint
  std::string::size_type pos = 0;
  while ((pos = code->find("%", pos)) != std::string::npos &&
         pos <= code->length() - 3) {
    int codepoint = strtol(code->substr(pos + 1, 2).c_str(), nullptr, 16);
    if (codepoint >= 33 && codepoint <= 127) {
      code->replace(pos, 3, std::string(1, codepoint));
    }
    pos += 3;
  }
}

// Checks the supplied Google authentication URL. If the URL indicated the user
// has aborted the flow or an error occured these are reported as error
// statuses, otherwise a status of OK is returned. If the URL contains an auth
// code query parameter, this will be returned in |auth_code|.
fuchsia::auth::AuthProviderStatus ParseAuthCodeFromUrl(const std::string& url,
                                                       std::string& auth_code) {
  static const std::string success_prefix =
      std::string{kRedirectUri} + "?code=";
  static const std::string cancel_prefix =
      std::string{kRedirectUri} + "?error=access_denied";

  if (url.find(cancel_prefix) == 0) {
    return fuchsia::auth::AuthProviderStatus::USER_CANCELLED;
  }
  if (url.find(success_prefix) != 0) {
    // The authentication process is still ongoing.
    return fuchsia::auth::AuthProviderStatus::OK;
  }

  // Take everything up to the next query parameter or hash fragment.
  auto end_char = url.find_first_of("#&", success_prefix.size());
  auto length = end_char == std::string::npos
                    ? std::string::npos
                    : end_char - success_prefix.size();
  auto code = url.substr(success_prefix.size(), length);
  NormalizeAuthCode(&code);

  if (code.empty()) {
    return fuchsia::auth::AuthProviderStatus::OAUTH_SERVER_ERROR;
  } else {
    auth_code = code;
    return fuchsia::auth::AuthProviderStatus::OK;
  }
}

}  // namespace

using auth_providers::oauth::OAuthRequestBuilder;
using auth_providers::oauth::ParseOAuthResponse;
using fuchsia::auth::AuthenticationUIContext;
using fuchsia::auth::AuthProviderStatus;
using fuchsia::auth::AuthTokenPtr;
using fuchsia::auth::FirebaseTokenPtr;
using modular::JsonValueToPrettyString;

GoogleAuthProviderImpl::GoogleAuthProviderImpl(
    async_dispatcher_t* const main_dispatcher, sys::ComponentContext* context,
    network_wrapper::NetworkWrapper* network_wrapper, Settings settings,
    fidl::InterfaceRequest<fuchsia::auth::AuthProvider> request)
    : main_dispatcher_(main_dispatcher),
      context_(context),
      network_wrapper_(network_wrapper),
      settings_(std::move(settings)),
      binding_(this, std::move(request)) {
  FXL_DCHECK(main_dispatcher_);
  FXL_DCHECK(network_wrapper_);

  // The class shuts down when the client connection is disconnected.
  binding_.set_error_handler([this](zx_status_t status) {
    if (on_empty_) {
      on_empty_();
    }
  });
}

GoogleAuthProviderImpl::~GoogleAuthProviderImpl() {}

void GoogleAuthProviderImpl::GetPersistentCredential(
    fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
    fidl::StringPtr user_profile_id, GetPersistentCredentialCallback callback) {
  FXL_DCHECK(auth_ui_context);
  get_persistent_credential_callback_ = std::move(callback);

  std::string url = GetAuthorizeUrl(user_profile_id);
  fuchsia::ui::views::ViewHolderToken view_holder_token = SetupChromium();
  if (!view_holder_token.value) {
    return;
  }
  fuchsia::web::NavigationControllerPtr controller;
  web_frame_->GetNavigationController(controller.NewRequest());
  controller->LoadUrl(url, fuchsia::web::LoadUrlParams(),
                      [](fuchsia::web::NavigationController_LoadUrl_Result) {});
  FX_LOGF(INFO, NULL, "Loading URL: %s", url.c_str());

  auth_ui_context_ = auth_ui_context.Bind();
  auth_ui_context_.set_error_handler([this](zx_status_t status) {
    FX_LOG(INFO, NULL, "Overlay cancelled by the caller");
    ReleaseResources();
    RemoveCredentialInjectorInterface();
    SafelyCallbackGetPersistentCredential(AuthProviderStatus::UNKNOWN_ERROR,
                                          nullptr, nullptr);
    return;
  });

  auth_ui_context_->StartOverlay(std::move(view_holder_token));
  ExposeCredentialInjectorInterface();
}

void GoogleAuthProviderImpl::GetAppAccessToken(
    std::string credential, fidl::StringPtr app_client_id,
    const std::vector<std::string> app_scopes,
    GetAppAccessTokenCallback callback) {
  if (credential.empty()) {
    callback(AuthProviderStatus::BAD_REQUEST, nullptr);
    return;
  }

  auto request =
      OAuthRequestBuilder(kGoogleOAuthTokenEndpoint, "POST")
          .SetUrlEncodedBody("refresh_token=" + credential +
                             "&client_id=" + GetClientId(app_client_id.get()) +
                             "&grant_type=refresh_token");

  auto request_factory = [request = std::move(request)] {
    return request.Build();
  };

  Request(std::move(request_factory),
          [callback = std::move(callback)](http::URLResponse response) {
            auto oauth_response = ParseOAuthResponse(std::move(response));
            if (oauth_response.status != AuthProviderStatus::OK) {
              LogOauthResponse("GetAppAccessToken", oauth_response);
              callback(oauth_response.status, nullptr);
              return;
            }

            const char kRootSchema[] = R"({
              "type": "object",
              "properties": {
                "access_token": {
                  "type": "string"
                },
                "expires_in": {
                  "type": "integer"
                }
              },
              "required": ["access_token", "expires_in"]
            })";
            auto root_schema = rapidjson_utils::InitSchema(kRootSchema);
            if (!root_schema) {
              callback(AuthProviderStatus::INTERNAL_ERROR, nullptr);
              return;
            }
            if (!rapidjson_utils::ValidateSchema(oauth_response.json_response,
                                                 *root_schema)) {
              callback(AuthProviderStatus::OAUTH_SERVER_ERROR, nullptr);
              return;
            }

            AuthTokenPtr access_token = fuchsia::auth::AuthToken::New();
            access_token->token_type = fuchsia::auth::TokenType::ACCESS_TOKEN;
            access_token->token =
                oauth_response.json_response["access_token"].GetString();
            access_token->expires_in =
                oauth_response.json_response["expires_in"].GetUint64();

            callback(AuthProviderStatus::OK, std::move(access_token));
          });
}

void GoogleAuthProviderImpl::GetAppIdToken(std::string credential,
                                           fidl::StringPtr audience,
                                           GetAppIdTokenCallback callback) {
  if (credential.empty()) {
    callback(AuthProviderStatus::BAD_REQUEST, nullptr);
    return;
  }

  auto request =
      OAuthRequestBuilder(kGoogleOAuthTokenEndpoint, "POST")
          .SetUrlEncodedBody("refresh_token=" + credential +
                             "&client_id=" + GetClientId(audience.get()) +
                             "&grant_type=refresh_token");

  auto request_factory = [request = std::move(request)] {
    return request.Build();
  };
  Request(std::move(request_factory),
          [callback = std::move(callback)](http::URLResponse response) {
            auto oauth_response = ParseOAuthResponse(std::move(response));
            if (oauth_response.status != AuthProviderStatus::OK) {
              LogOauthResponse("GetAppIdToken", oauth_response);
              callback(oauth_response.status, nullptr);
              return;
            }

            const char kRootSchema[] = R"({
              "type": "object",
              "properties": {
                "id_token": {
                  "type": "string"
                },
                "expires_in": {
                  "type": "integer"
                }
              },
              "required": ["id_token", "expires_in"]
            })";
            auto root_schema = rapidjson_utils::InitSchema(kRootSchema);
            if (!root_schema) {
              callback(AuthProviderStatus::INTERNAL_ERROR, nullptr);
              return;
            }
            if (!rapidjson_utils::ValidateSchema(oauth_response.json_response,
                                                 *root_schema)) {
              callback(AuthProviderStatus::OAUTH_SERVER_ERROR, nullptr);
              return;
            }

            AuthTokenPtr id_token = fuchsia::auth::AuthToken::New();
            id_token->token =
                oauth_response.json_response["id_token"].GetString();
            id_token->token_type = fuchsia::auth::TokenType::ID_TOKEN;
            id_token->expires_in =
                oauth_response.json_response["expires_in"].GetUint64();

            callback(AuthProviderStatus::OK, std::move(id_token));
          });
}

void GoogleAuthProviderImpl::GetAppFirebaseToken(
    std::string id_token, std::string firebase_api_key,
    GetAppFirebaseTokenCallback callback) {
  if (id_token.empty() || firebase_api_key.empty()) {
    callback(AuthProviderStatus::BAD_REQUEST, nullptr);
    return;
  }

  std::map<std::string, std::string> query_params;
  query_params["key"] = firebase_api_key;
  auto request = OAuthRequestBuilder(kFirebaseAuthEndpoint, "POST")
                     .SetQueryParams(query_params)
                     .SetJsonBody(R"({"postBody": "id_token=)" + id_token +
                                  R"(&providerId=google.com",)" +
                                  R"("returnIdpCredential": true,)" +
                                  R"("returnSecureToken": true,)" +
                                  R"("requestUri": "http://localhost"})");

  // Exchange credential to access token at Google OAuth token endpoint
  auto request_factory = [request = std::move(request)] {
    return request.Build();
  };
  Request(std::move(request_factory), [callback = std::move(callback)](
                                          http::URLResponse response) {
    auto oauth_response = ParseOAuthResponse(std::move(response));
    if (oauth_response.status != AuthProviderStatus::OK) {
      LogOauthResponse("GetAppFirebaseToken", oauth_response);
      callback(oauth_response.status, nullptr);
      return;
    }

    const char kRootSchema[] = R"({
      "type": "object",
      "properties": {
        "idToken": {
          "type": "string"
        },
        "email": {
          "type": "string"
        },
        "localId": {
          "type": "string"
        },
        "expiresIn": {
          "type": "string"
        }
      },
      "required": ["idToken", "email", "localId", "expiresIn"]
    })";
    auto root_schema = rapidjson_utils::InitSchema(kRootSchema);
    if (!root_schema) {
      callback(AuthProviderStatus::INTERNAL_ERROR, nullptr);
      return;
    }
    if (!rapidjson_utils::ValidateSchema(oauth_response.json_response,
                                         *root_schema)) {
      callback(AuthProviderStatus::OAUTH_SERVER_ERROR, nullptr);
      return;
    }

    FirebaseTokenPtr fb_token = fuchsia::auth::FirebaseToken::New();
    fb_token->id_token = oauth_response.json_response["idToken"].GetString();
    fb_token->email = oauth_response.json_response["email"].GetString();
    fb_token->local_id = oauth_response.json_response["localId"].GetString();
    fb_token->expires_in =
        std::atoll(oauth_response.json_response["expiresIn"].GetString());

    callback(AuthProviderStatus::OK, std::move(fb_token));
  });
}

void GoogleAuthProviderImpl::RevokeAppOrPersistentCredential(
    std::string credential, RevokeAppOrPersistentCredentialCallback callback) {
  if (credential.empty()) {
    callback(AuthProviderStatus::BAD_REQUEST);
    return;
  }

  std::string url =
      kGoogleRevokeTokenEndpoint + std::string("?token=") + credential;
  auto request = OAuthRequestBuilder(url, "POST").SetUrlEncodedBody("");

  auto request_factory = [request = std::move(request)] {
    return request.Build();
  };

  Request(std::move(request_factory),
          [callback = std::move(callback)](http::URLResponse response) {
            auto oauth_response = ParseOAuthResponse(std::move(response));
            if (oauth_response.status != AuthProviderStatus::OK) {
              LogOauthResponse("RevokeToken", oauth_response);
              callback(oauth_response.status);
              return;
            }

            callback(AuthProviderStatus::OK);
          });
}

void GoogleAuthProviderImpl::GetPersistentCredentialFromAttestationJWT(
    fidl::InterfaceHandle<AttestationSigner> attestation_signer,
    AttestationJWTParams jwt_params,
    fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
    fidl::StringPtr user_profile_id,
    GetPersistentCredentialFromAttestationJWTCallback callback) {
  // Remote attestation flow not supported for traditional OAuth.
  callback(AuthProviderStatus::BAD_REQUEST, nullptr, nullptr, nullptr, nullptr);
}

void GoogleAuthProviderImpl::GetAppAccessTokenFromAssertionJWT(
    fidl::InterfaceHandle<AttestationSigner> attestation_signer,
    AssertionJWTParams jwt_params, std::string credential,
    const std::vector<std::string> scopes,
    GetAppAccessTokenFromAssertionJWTCallback callback) {
  // Remote attestation flow not supported for traditional OAuth.
  callback(AuthProviderStatus::BAD_REQUEST, nullptr, nullptr, nullptr);
}

void GoogleAuthProviderImpl::OnNavigationStateChanged(
    NavigationState change, OnNavigationStateChangedCallback callback) {
  // Not all events change the URL, those that don't can be ignored.
  if (!change.has_url()) {
    callback();
    return;
  }

  std::string auth_code;
  AuthProviderStatus status = ParseAuthCodeFromUrl(change.url(), auth_code);

  // If either an error occured or the user successfully received an auth code
  // we need to close the browser instance.
  if (status != AuthProviderStatus::OK || !auth_code.empty()) {
    ReleaseResources();
    // InjectPersistentCredential will still be reachable even after removing
    // the interface from output, but any requests to it will be discarded.
    RemoveCredentialInjectorInterface();
    if (status != AuthProviderStatus::OK) {
      FX_LOGF(INFO, NULL, "Failed to capture auth code: Status %d", status);
      SafelyCallbackGetPersistentCredential(status, nullptr, nullptr);
    } else if (!auth_code.empty()) {
      FX_LOGF(INFO, NULL, "Captured auth code of length %d", auth_code.size());
      ExchangeAuthCode(auth_code);
    }
  }

  callback();
}

void GoogleAuthProviderImpl::InjectPersistentCredential(
    fuchsia::auth::UserProfileInfoPtr user_profile_info,
    std::string credential) {
  ReleaseResources();
  RemoveCredentialInjectorInterface();
  FX_LOGF(INFO, NULL, "Received injection request with credential of length %d",
          credential.size());
  SafelyCallbackGetPersistentCredential(AuthProviderStatus::OK,
                                        std::move(credential),
                                        std::move(user_profile_info));
}

std::string GoogleAuthProviderImpl::GetAuthorizeUrl(
    fidl::StringPtr user_profile) {
  // TODO(ukode,jsankey): use app_scopes instead of |kScopes|.
  const std::vector<std::string> scopes(kScopes.begin(), kScopes.end());
  std::string scopes_str = fxl::JoinStrings(scopes, "+");

  std::string url = settings_.use_dedicated_endpoint ? kGoogleFuchsiaEndpoint
                                                     : kGoogleOAuthAuthEndpoint;
  url += "?scope=" + scopes_str;
  url += "&glif=";
  url += settings_.use_glif ? "true" : "false";
  url += "&response_type=code&redirect_uri=";
  url += kRedirectUri;
  url += "&client_id=";
  url += kFuchsiaClientId;
  // TODO(ukode,jsankey): Set user_profile_id in the state query arg for re-auth
  // This probably involves moving the current implementation of UrlEncoding in
  // OAuthRequestBuilder to a reusable library and using this to urlencode the
  // supplied user_profile into the login_hint query parameter.
  return url;
}

void GoogleAuthProviderImpl::ExchangeAuthCode(std::string auth_code) {
  auto request = OAuthRequestBuilder(kGoogleOAuthTokenEndpoint, "POST")
                     .SetUrlEncodedBody("code=" + auth_code +
                                        "&redirect_uri=" + kRedirectUri +
                                        "&client_id=" + kFuchsiaClientId +
                                        "&grant_type=authorization_code");

  auto request_factory = [request = std::move(request)] {
    return request.Build();
  };

  Request(std::move(request_factory), [this](http::URLResponse response) {
    auto oauth_response = ParseOAuthResponse(std::move(response));
    if (oauth_response.status != AuthProviderStatus::OK) {
      LogOauthResponse("ExchangeAuthCode", oauth_response);
      SafelyCallbackGetPersistentCredential(oauth_response.status, nullptr,
                                            nullptr);
      return;
    }

    const char kRootSchema[] = R"({
      "type": "object",
      "properties": {
        "refresh_token": {
          "type": "string"
        },
        "access_token": {
          "type": "string"
        }
      },
      "required": ["refresh_token", "access_token"]
    })";
    auto root_schema = rapidjson_utils::InitSchema(kRootSchema);
    if (!root_schema) {
      SafelyCallbackGetPersistentCredential(AuthProviderStatus::INTERNAL_ERROR,
                                            nullptr, nullptr);
      return;
    }
    if (!rapidjson_utils::ValidateSchema(oauth_response.json_response,
                                         *root_schema)) {
      FX_LOGF(WARNING, NULL,
              "Got response without refresh and access tokens: %s",
              JsonValueToPrettyString(oauth_response.json_response).c_str());
      SafelyCallbackGetPersistentCredential(
          AuthProviderStatus::OAUTH_SERVER_ERROR, nullptr, nullptr);
      return;
    }

    auto refresh_token =
        oauth_response.json_response["refresh_token"].GetString();
    auto access_token =
        oauth_response.json_response["access_token"].GetString();
    FX_LOGF(INFO, NULL, "Received refresh token of length %d",
            strlen(refresh_token));

    GetUserProfile(refresh_token, access_token);
  });
}

void GoogleAuthProviderImpl::GetUserProfile(fidl::StringPtr credential,
                                            fidl::StringPtr access_token) {
  FXL_DCHECK(credential.get().size() > 0);
  FXL_DCHECK(access_token.get().size() > 0);

  auto request = OAuthRequestBuilder(kGoogleUserInfoEndpoint, "GET")
                     .SetAuthorizationHeader(access_token.get());

  auto request_factory = [request = std::move(request)] {
    return request.Build();
  };

  Request(std::move(request_factory), [this,
                                       credential](http::URLResponse response) {
    fuchsia::auth::UserProfileInfoPtr user_profile_info =
        fuchsia::auth::UserProfileInfo::New();

    auto oauth_response = ParseOAuthResponse(std::move(response));
    if (oauth_response.status != AuthProviderStatus::OK) {
      LogOauthResponse("UserInfo", oauth_response);
      SafelyCallbackGetPersistentCredential(oauth_response.status, credential,
                                            std::move(user_profile_info));
      return;
    }

    if (oauth_response.json_response.HasMember("sub") &&
        oauth_response.json_response["sub"].IsString()) {
      user_profile_info->id = oauth_response.json_response["sub"].GetString();
    } else {
      LogOauthResponse("UserInfo", oauth_response);
      FX_LOG(INFO, NULL, "Missing unique identifier in UserInfo response");
      SafelyCallbackGetPersistentCredential(
          AuthProviderStatus::OAUTH_SERVER_ERROR, nullptr,
          std::move(user_profile_info));
      return;
    }

    if (oauth_response.json_response.HasMember("name") &&
        oauth_response.json_response["name"].IsString()) {
      user_profile_info->display_name =
          oauth_response.json_response["name"].GetString();
    }

    if (oauth_response.json_response.HasMember("profile") &&
        oauth_response.json_response["profile"].IsString()) {
      user_profile_info->url =
          oauth_response.json_response["profile"].GetString();
    }

    if (oauth_response.json_response.HasMember("picture") &&
        oauth_response.json_response["picture"].IsString()) {
      user_profile_info->image_url =
          oauth_response.json_response["picture"].GetString();
    }

    FX_LOG(INFO, NULL, "Received valid UserInfo response");
    SafelyCallbackGetPersistentCredential(oauth_response.status, credential,
                                          std::move(user_profile_info));
  });
}

fuchsia::ui::views::ViewHolderToken GoogleAuthProviderImpl::SetupChromium() {
  // Connect to the Chromium service and create a new frame.
  auto context_provider =
      context_->svc()->Connect<fuchsia::web::ContextProvider>();

  fidl::InterfaceHandle<fuchsia::io::Directory> incoming_service_clone =
      context_->svc()->CloneChannel();

  if (!incoming_service_clone.is_valid()) {
    FX_LOG(ERROR, NULL, "Failed to clone service directory");
    return fuchsia::ui::views::ViewHolderToken();
  }

  fuchsia::web::CreateContextParams params;
  params.set_service_directory(std::move(incoming_service_clone));
  context_provider->Create(std::move(params), web_context_.NewRequest());
  web_context_->CreateFrame(web_frame_.NewRequest());

  // Bind ourselves as a NavigationEventListener on this frame.
  fuchsia::web::NavigationEventListenerPtr navigation_event_listener;
  navigation_event_listener_bindings_.AddBinding(
      this, navigation_event_listener.NewRequest());
  web_frame_->SetNavigationEventListener(std::move(navigation_event_listener));

  // And create a view for the frame.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  web_frame_->CreateView(std::move(view_token));

  return std::move(view_holder_token);
}

void GoogleAuthProviderImpl::SafelyCallbackGetPersistentCredential(
    AuthProviderStatus auth_provider_status, fidl::StringPtr credential,
    fuchsia::auth::UserProfileInfoPtr user_profile_info) {
  if (get_persistent_credential_callback_) {
    get_persistent_credential_callback_(auth_provider_status,
                                        std::move(credential),
                                        std::move(user_profile_info));
    get_persistent_credential_callback_ = nullptr;
  } else {
    FX_LOG(WARNING, NULL,
           "Attempted to call GetPersistentCredential callback twice.");
  }
}

void GoogleAuthProviderImpl::ReleaseResources() {
  // Close any open view
  if (auth_ui_context_) {
    FX_LOG(INFO, NULL, "Releasing Auth UI Context");
    auth_ui_context_.set_error_handler(nullptr);
    auth_ui_context_->StopOverlay();
    auth_ui_context_ = nullptr;
  }
  // Release all smart pointers for Chromium resources.
  web_frame_ = nullptr;
  web_context_ = nullptr;
}

void GoogleAuthProviderImpl::ExposeCredentialInjectorInterface() {
  context_->outgoing()->debug_dir()->AddEntry(
      kInjectionEntry,
      std::make_unique<vfs::Service>(injector_bindings_.GetHandler(this)));
}

void GoogleAuthProviderImpl::RemoveCredentialInjectorInterface() {
  if (context_->outgoing()->debug_dir()->RemoveEntry(kInjectionEntry) ==
      ZX_ERR_NOT_FOUND) {
    FX_LOGF(WARNING, NULL,
            "Attempted to remove nonexistent '%s' from debug directory",
            kInjectionEntry);
  }
}

void GoogleAuthProviderImpl::Request(
    fit::function<http::URLRequest()> request_factory,
    fit::function<void(http::URLResponse response)> callback) {
  requests_.emplace(network_wrapper_->Request(std::move(request_factory),
                                              std::move(callback)));
}

}  // namespace google_auth_provider
