// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// OAuthTokenManagerApp is a simple auth service hack for fetching user OAuth
// tokens to talk programmatically to backend apis. These apis are hosted or
// integrated with Identity providers such as Google, Twitter, Spotify etc.

#include <iomanip>
#include <iostream>
#include <map>
#include <memory>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/auth/account_provider.fidl.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "apps/modular/src/oauth_token_manager/credentials_generated.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/web_runner/services/web_view.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/join_strings.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"
#include "third_party/rapidjson/rapidjson/error/en.h"

namespace modular {
namespace auth {

using FirebaseTokenCallback =
    std::function<void(modular::auth::FirebaseTokenPtr)>;

namespace {

// TODO(alhaad/ukode): Move the following to a configuration file.
// NOTE: We are currently using a single client-id in Fuchsia. This is temporary
// and will change in the future.
constexpr char kClientId[] =
    "1051596886047-kjfjv6tuoluj61n5cedv71ansrj3ggi8.apps."
    "googleusercontent.com";
constexpr char kGoogleOAuthAuthEndpoint[] =
    "https://accounts.google.com/o/oauth2/v2/auth";
constexpr char kGoogleOAuthTokenEndpoint[] =
    "https://www.googleapis.com/oauth2/v4/token";
constexpr char kGooglePeopleGetEndpoint[] =
    "https://www.googleapis.com/plus/v1/people/me";
constexpr char kFirebaseAuthEndpoint[] =
    "https://www.googleapis.com/identitytoolkit/v3/relyingparty/"
    "verifyAssertion";
constexpr char kRedirectUri[] = "com.google.fuchsia.auth:/oauth2redirect";
constexpr char kCredentialsFile[] = "/data/modular/device/v2/creds.db";
constexpr char kWebViewUrl[] = "file:///system/apps/web_view";

constexpr auto kScopes = {"openid",
                          "email",
                          "https://www.googleapis.com/auth/assistant",
                          "https://www.googleapis.com/auth/gmail.modify",
                          "https://www.googleapis.com/auth/userinfo.email",
                          "https://www.googleapis.com/auth/userinfo.profile",
                          "https://www.googleapis.com/auth/youtube.readonly",
                          "https://www.googleapis.com/auth/contacts",
                          "https://www.googleapis.com/auth/plus.login"};

// Type of token requested.
enum TokenType {
  ACCESS_TOKEN = 0,
  ID_TOKEN = 1,
  FIREBASE_JWT_TOKEN = 2,
};

// TODO(alhaad/ukode): Don't use a hand-rolled version of this.
std::string UrlEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (std::string::const_iterator i = value.begin(), n = value.end(); i != n;
       ++i) {
    std::string::value_type c = (*i);

    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '=' ||
        c == '&' || c == '+') {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << std::uppercase;
    escaped << '%' << std::setw(2) << int((unsigned char)c);
    escaped << std::nouppercase;
  }

  return escaped.str();
}

// Returns |::auth::CredentialStore| after parsing credentials from
// |kCredentialsFile|.
const ::auth::CredentialStore* ParseCredsFile() {
  // Reserialize existing users.
  if (!files::IsFile(kCredentialsFile)) {
    return nullptr;
  }

  std::string serialized_creds;
  if (!files::ReadFileToString(kCredentialsFile, &serialized_creds)) {
    FTL_LOG(WARNING) << "Unable to read user configuration file at: "
                     << kCredentialsFile;
    return nullptr;
  }

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(serialized_creds.data()),
      serialized_creds.size());
  if (!::auth::VerifyCredentialStoreBuffer(verifier)) {
    FTL_LOG(WARNING) << "Unable to verify credentials buffer:"
                     << serialized_creds.data();
    return nullptr;
  }

  return ::auth::GetCredentialStore(serialized_creds.data());
}

// Serializes |::auth::CredentialStore| to the |kCredentialsFIle| on disk.
bool WriteCredsFile(const std::string& serialized_creds) {
  // verify file before saving
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(serialized_creds.data()),
      serialized_creds.size());
  if (!::auth::VerifyCredentialStoreBuffer(verifier)) {
    FTL_LOG(ERROR) << "Unable to verify credentials buffer:"
                   << serialized_creds.data();
    return false;
  }

  if (!files::CreateDirectory(files::GetDirectoryName(kCredentialsFile))) {
    FTL_LOG(ERROR) << "Unable to create directory for " << kCredentialsFile;
    return false;
  }

  if (!files::WriteFile(kCredentialsFile, serialized_creds.data(),
                        serialized_creds.size())) {
    FTL_LOG(ERROR) << "Unable to write file " << kCredentialsFile;
    return false;
  }

  return true;
}

// Exactly one of success_callback and failure_callback is ever invoked.
void Post(const std::string& request_body,
          network::URLLoader* const url_loader,
          const std::string& url,
          const std::function<void()>& success_callback,
          const std::function<void(std::string)>& failure_callback,
          const std::function<bool(rapidjson::Document)>& set_token_callback) {
  std::string encoded_request_body(request_body);
  if (url.find(kFirebaseAuthEndpoint) == std::string::npos) {
    encoded_request_body = UrlEncode(request_body);
  }

  mx::vmo data;
  auto result = mtl::VmoFromString(encoded_request_body, &data);
  FTL_VLOG(1) << "Post Data:" << encoded_request_body;
  FTL_DCHECK(result);

  network::URLRequestPtr request(network::URLRequest::New());
  request->url = url;
  request->method = "POST";
  request->auto_follow_redirects = true;

  // Content-length header.
  network::HttpHeaderPtr content_length_header = network::HttpHeader::New();
  content_length_header->name = "Content-length";
  uint64_t data_size = encoded_request_body.length();
  content_length_header->value = ftl::NumberToString(data_size);
  request->headers.push_back(std::move(content_length_header));

  // content-type header.
  network::HttpHeaderPtr content_type_header = network::HttpHeader::New();
  content_type_header->name = "content-type";
  if (url.find("identitytoolkit") != std::string::npos) {
    // set accept header
    network::HttpHeaderPtr accept_header = network::HttpHeader::New();
    accept_header->name = "accept";
    accept_header->value = "application/json";
    request->headers.push_back(std::move(accept_header));

    // set content_type header
    content_type_header->value = "application/json";
  } else {
    content_type_header->value = "application/x-www-form-urlencoded";
  }
  request->headers.push_back(std::move(content_type_header));

  request->body = network::URLBody::New();
  request->body->set_buffer(std::move(data));

  url_loader->Start(std::move(request), [success_callback, failure_callback,
                                         set_token_callback](
                                            network::URLResponsePtr response) {
    if (!response->error.is_null()) {
      failure_callback(
          "Network error! code: " + std::to_string(response->error->code) +
          " description: " + response->error->description.data());
      return;
    }

    std::string response_body;
    if (!response->body.is_null()) {
      FTL_DCHECK(response->body->is_stream());
      // TODO(alhaad/ukode): Use non-blocking variant.
      if (!mtl::BlockingCopyToString(std::move(response->body->get_stream()),
                                     &response_body)) {
        failure_callback("Failed to read response from socket with status:" +
                         std::to_string(response->status_code));
        return;
      }
    }

    if (response->status_code != 200) {
      failure_callback(
          "Status code: " + std::to_string(response->status_code) +
          " while fetching tokens with error description:" + response_body);
      return;
    }

    rapidjson::Document doc;
    rapidjson::ParseResult ok = doc.Parse(response_body);
    if (!ok) {
      std::string error_msg = GetParseError_En(ok.Code());
      failure_callback("JSON parse error: " + error_msg);
      return;
    };
    auto result = set_token_callback(std::move(doc));
    FTL_DCHECK(result);
    success_callback();
  });
}

// Exactly one of success_callback and failure_callback is ever invoked.
void Get(network::URLLoader* const url_loader,
         const std::string& url,
         const std::string& access_token,
         const std::function<void()>& success_callback,
         const std::function<void(std::string)>& failure_callback,
         const std::function<bool(rapidjson::Document)>& set_token_callback) {
  network::URLRequestPtr request(network::URLRequest::New());
  request->url = url;
  request->method = "GET";
  request->auto_follow_redirects = true;

  // Set Authorization header.
  network::HttpHeaderPtr auth_header = network::HttpHeader::New();
  auth_header->name = "Authorization";
  auth_header->value = "Bearer " + access_token;
  request->headers.push_back(std::move(auth_header));

  // set content-type header to json.
  network::HttpHeaderPtr content_type_header = network::HttpHeader::New();
  content_type_header->name = "content-type";
  content_type_header->value = "application/json";

  // set accept header to json
  network::HttpHeaderPtr accept_header = network::HttpHeader::New();
  accept_header->name = "accept";
  accept_header->value = "application/json";
  request->headers.push_back(std::move(accept_header));

  url_loader->Start(std::move(request), [success_callback, failure_callback,
                                         set_token_callback](
                                            network::URLResponsePtr response) {
    if (!response->error.is_null()) {
      failure_callback(
          "Network error! code: " + std::to_string(response->error->code) +
          " description: " + response->error->description.data());
      return;
    }

    std::string response_body;
    if (!response->body.is_null()) {
      FTL_DCHECK(response->body->is_stream());
      // TODO(alhaad/ukode): Use non-blocking variant.
      if (!mtl::BlockingCopyToString(std::move(response->body->get_stream()),
                                     &response_body)) {
        failure_callback("Failed to read response from socket with status:" +
                         std::to_string(response->status_code));
        return;
      }
    }

    if (response->status_code != 200) {
      failure_callback(
          "Status code: " + std::to_string(response->status_code) +
          " while fetching tokens with error description:" + response_body);
      return;
    }

    rapidjson::Document doc;
    rapidjson::ParseResult ok = doc.Parse(response_body);
    if (!ok) {
      std::string error_msg = GetParseError_En(ok.Code());
      failure_callback("JSON parse error: " + error_msg);
      return;
    };
    auto result = set_token_callback(std::move(doc));
    FTL_DCHECK(result);
    success_callback();
  });
}

}  // namespace

// Implementation of the OAuth Token Manager app.
class OAuthTokenManagerApp : AccountProvider {
 public:
  OAuthTokenManagerApp();

 private:
  // |AccountProvider|
  void Initialize(
      fidl::InterfaceHandle<AccountProviderContext> provider) override;

  // |AccountProvider|
  void AddAccount(IdentityProvider identity_provider,
                  const AddAccountCallback& callback) override;

  // |AccountProvider|
  void GetTokenProviderFactory(
      const fidl::String& account_id,
      fidl::InterfaceRequest<TokenProviderFactory> request) override;

  // Generate a random account id.
  std::string GenerateAccountId();

  // Refresh access and id tokens.
  void RefreshToken(const std::string& account_id,
                    const TokenType& token_type,
                    const std::function<void(std::string)>& callback);

  // Refresh firebase tokens.
  void RefreshFirebaseToken(const std::string& account_id,
                            const std::string& firebase_api_key,
                            const std::string& id_token,
                            const FirebaseTokenCallback& callback);

  std::shared_ptr<app::ApplicationContext> application_context_;

  AccountProviderContextPtr account_provider_context_;

  fidl::Binding<AccountProvider> binding_;

  class TokenProviderFactoryImpl;
  // account_id -> TokenProviderFactoryImpl
  std::unordered_map<std::string, std::unique_ptr<TokenProviderFactoryImpl>>
      token_provider_factory_impls_;

  // In-memory cache for long lived user credentials. This cache is populated
  // from |kCredentialsFile| on Initialize.
  // TODO(ukode): Replace rapidjson with a dedicated data structure for storing
  // user credentials.
  const ::auth::CredentialStore* creds_ = nullptr;

  // In-memory cache for short lived firebase auth id tokens. These tokens get
  // reset on system reboots. Tokens are cached based on the expiration time
  // set by the Firebase servers. Cache is indexed by firebase api keys.
  struct FirebaseAuthToken {
    uint64_t creation_ts;
    uint64_t expires_in;
    std::string id_token;
    std::string local_id;
    std::string email;
  };

  // In-memory cache for short lived oauth tokens that resets on system reboots.
  // Tokens are cached based on the expiration time set by the Identity
  // provider. Cache is indexed by unique account_ids.
  struct ShortLivedToken {
    uint64_t creation_ts;
    uint64_t expires_in;
    std::string access_token;
    std::string id_token;
    std::map<std::string, FirebaseAuthToken> fb_tokens_;
  };
  std::map<std::string, ShortLivedToken> oauth_tokens_;

  // We are using operations here not to guard state across asynchronous calls
  // but rather to clean up state after an 'operation' is done.
  // TODO(ukode): All operations are running in a queue now which is
  // inefficient because we block on operations that could be done in parallel.
  // Instead we may want to create an operation for what
  // TokenProviderFactoryImpl::GetFirebaseAuthToken() is doing in an sub
  // operation queue.
  OperationQueue operation_queue_;

  class GoogleFirebaseTokensCall;
  class GoogleOAuthTokensCall;
  class GoogleUserCredsCall;
  class GoogleProfileAttributesCall;

  FTL_DISALLOW_COPY_AND_ASSIGN(OAuthTokenManagerApp);
};

class OAuthTokenManagerApp::TokenProviderFactoryImpl : TokenProviderFactory,
                                                       TokenProvider {
 public:
  TokenProviderFactoryImpl(const fidl::String& account_id,
                           OAuthTokenManagerApp* const app,
                           fidl::InterfaceRequest<TokenProviderFactory> request)
      : account_id_(account_id), binding_(this, std::move(request)), app_(app) {
    binding_.set_connection_error_handler(
        [this] { app_->token_provider_factory_impls_.erase(account_id_); });
  }

 private:
  // |TokenProviderFactory|
  void GetTokenProvider(
      const fidl::String& application_url,
      fidl::InterfaceRequest<TokenProvider> request) override {
    // TODO(alhaad/ukode): Current implementation is agnostic about which
    // agent is requesting what token. Fix this.
    token_provider_bindings_.AddBinding(this, std::move(request));
  }

  // |TokenProvider|
  void GetAccessToken(const GetAccessTokenCallback& callback) override {
    FTL_DCHECK(app_);
    app_->RefreshToken(account_id_, ACCESS_TOKEN, callback);
  }

  // |TokenProvider|
  void GetIdToken(const GetIdTokenCallback& callback) override {
    FTL_DCHECK(app_);
    app_->RefreshToken(account_id_, ID_TOKEN, callback);
  }

  // |TokenProvider|
  void GetFirebaseAuthToken(
      const fidl::String& firebase_api_key,
      const GetFirebaseAuthTokenCallback& callback) override {
    FTL_DCHECK(app_);

    // Oauth id token is used as input to fetch firebase auth token.
    GetIdToken([ this, firebase_api_key = firebase_api_key,
                 callback ](const std::string id_token) {
      app_->RefreshFirebaseToken(account_id_, firebase_api_key, id_token,
                                 callback);
    });
  }

  // |TokenProvider|
  void GetClientId(const GetClientIdCallback& callback) override {
    callback(kClientId);
  }

  std::string account_id_;
  fidl::Binding<TokenProviderFactory> binding_;
  fidl::BindingSet<TokenProvider> token_provider_bindings_;

  OAuthTokenManagerApp* const app_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TokenProviderFactoryImpl);
};

class OAuthTokenManagerApp::GoogleFirebaseTokensCall
    : Operation<modular::auth::FirebaseTokenPtr> {
 public:
  GoogleFirebaseTokensCall(OperationContainer* const container,
                           const std::string& account_id,
                           const std::string& firebase_api_key,
                           const std::string& id_token,
                           OAuthTokenManagerApp* const app,
                           const FirebaseTokenCallback& callback)
      : Operation(container, callback),
        account_id_(account_id),
        firebase_api_key_(firebase_api_key),
        id_token_(id_token),
        app_(app) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &firebase_token_};

    if (account_id_.empty()) {
      Failure(flow, "Account id is empty, running in guest mode.");
      return;
    }

    if (firebase_api_key_.empty()) {
      Failure(flow, "Firebase Api key is empty");
      return;
    }

    if (id_token_.empty()) {
      // TODO(ukode): Need to differentiate between deleted users, users that
      // are not provisioned and Guest mode users. For now, return empty
      // response in such cases as there is no clear way to differentiate
      // between regular users and guest users.
      Success(flow);
      return;
    }

    // check cache for existing firebase tokens.
    bool cacheValid = HasCacheExpired();
    if (!cacheValid) {
      FetchFirebaseToken(flow);
    }
    Success(flow);
    return;
  }

  // Fetch fresh firebase auth token by exchanging idToken from Google.
  void FetchFirebaseToken(FlowToken flow) {
    FTL_DCHECK(!id_token_.empty());
    FTL_DCHECK(!firebase_api_key_.empty());

    // JSON post request body
    const std::string json_request_body =
        "{  \"postBody\": \"id_token=" + id_token_ +
        "&providerId=google.com\"," + "   \"returnIdpCredential\": true," +
        "   \"returnSecureToken\": true," +
        "   \"requestUri\": \"http://localhost\"" + "}";

    app_->application_context_->ConnectToEnvironmentService(
        network_service_.NewRequest());
    network_service_->CreateURLLoader(url_loader_.NewRequest());

    std::string url(kFirebaseAuthEndpoint);
    url += "?key=" + UrlEncode(firebase_api_key_);

    // This flow exclusively branches below, so we need to put it in a shared
    // container from which it can be removed once for all branches.
    FlowTokenHolder branch{flow};

    Post(json_request_body, url_loader_.get(), url,
         [this, branch] {
           std::unique_ptr<FlowToken> flow = branch.Continue();
           FTL_CHECK(flow);
           Success(*flow);
         },
         [this, branch](const std::string error_message) {
           std::unique_ptr<FlowToken> flow = branch.Continue();
           FTL_CHECK(flow);
           Failure(*flow, error_message);
         },
         [this](rapidjson::Document doc) {
           return GetFirebaseToken(std::move(doc));
         });
  }

  // Parses firebase jwt auth token from firebase auth endpoint response and
  // saves it to local token in-memory cache.
  bool GetFirebaseToken(rapidjson::Document jwt_token) {
    FTL_VLOG(1) << "Firebase Token: "
                << modular::JsonValueToPrettyString(jwt_token);

    if (!jwt_token.HasMember("idToken") || !jwt_token.HasMember("localId") ||
        !jwt_token.HasMember("email") || !jwt_token.HasMember("expiresIn")) {
      FTL_LOG(ERROR)
          << "Firebase Token returned from server is missing "
          << "either idToken or email or localId fields. Returned token: "
          << modular::JsonValueToPrettyString(jwt_token);
      return false;
    }

    uint64_t expiresIn;
    std::istringstream(jwt_token["expiresIn"].GetString()) >> expiresIn;

    app_->oauth_tokens_[account_id_].fb_tokens_[firebase_api_key_] = {
        static_cast<uint64_t>(ftl::TimePoint::Now().ToEpochDelta().ToSeconds()),
        expiresIn,
        jwt_token["idToken"].GetString(),
        jwt_token["localId"].GetString(),
        jwt_token["email"].GetString(),
    };
    return true;
  }

  // Returns true if the access and idtokens stored in cache are expired.
  bool HasCacheExpired() {
    FTL_DCHECK(app_);
    FTL_DCHECK(!account_id_.empty());
    FTL_DCHECK(!firebase_api_key_.empty());

    if (app_->oauth_tokens_[account_id_].fb_tokens_.find(firebase_api_key_) ==
        app_->oauth_tokens_[account_id_].fb_tokens_.end()) {
      FTL_VLOG(1) << "Firebase api key: [" << firebase_api_key_
                  << "] not found in cache.";
      return false;
    }

    uint64_t current_ts = ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
    auto fb_token =
        app_->oauth_tokens_[account_id_].fb_tokens_[firebase_api_key_];
    uint64_t creation_ts = fb_token.creation_ts;
    uint64_t token_expiry = fb_token.expires_in;
    if ((current_ts - creation_ts) < token_expiry) {
      FTL_VLOG(1) << "Returning firebase token for api key ["
                  << firebase_api_key_ << "] from cache. ";
      return true;
    }

    return false;
  }

  void Success(FlowToken flow) {
    firebase_token_ = auth::FirebaseToken::New();
    if (id_token_.empty()) {
      firebase_token_->id_token = "";
      firebase_token_->local_id = "";
      firebase_token_->email = "";
    } else {
      auto fb_token =
          app_->oauth_tokens_[account_id_].fb_tokens_[firebase_api_key_];
      firebase_token_->id_token = fb_token.id_token;
      firebase_token_->local_id = fb_token.local_id;
      firebase_token_->email = fb_token.email;
    }
  }

  void Failure(FlowToken flow, const std::string& error_message) {
    FTL_LOG(ERROR) << error_message;
  }

  const std::string account_id_;
  const std::string firebase_api_key_;
  const std::string id_token_;
  OAuthTokenManagerApp* const app_;

  modular::auth::FirebaseTokenPtr firebase_token_;

  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GoogleFirebaseTokensCall);
};

class OAuthTokenManagerApp::GoogleOAuthTokensCall : Operation<fidl::String> {
 public:
  GoogleOAuthTokensCall(OperationContainer* const container,
                        const std::string& account_id,
                        const TokenType& token_type,
                        OAuthTokenManagerApp* const app,
                        const std::function<void(fidl::String)>& callback)
      : Operation(container, callback),
        account_id_(account_id),
        token_type_(token_type),
        app_(app) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    if (account_id_.empty()) {
      Failure(flow, "Account id is empty.");
      return;
    }

    FTL_VLOG(1) << "Fetching access/id tokens for Account_ID:" << account_id_;
    const std::string refresh_token = GetRefreshToken();
    if (refresh_token.empty()) {
      // TODO(ukode): Need to differentiate between deleted users, users that
      // are not provisioned and Guest mode users. For now, return empty
      // response in such cases as there is no clear way to differentiate
      // between regular users and guest users.
      Success(flow);
      return;
    }

    bool cacheValid = HasCacheExpired();
    if (!cacheValid) {
      // fetching tokens from server.
      FetchAccessAndIdToken(refresh_token, flow);
    }
    Success(flow);  // fetching tokens from local cache.
    return;
  }

  // Fetch fresh access and id tokens by exchanging refresh token from Google
  // token endpoint.
  void FetchAccessAndIdToken(const std::string& refresh_token, FlowToken flow) {
    FTL_CHECK(!refresh_token.empty());

    const std::string request_body = "refresh_token=" + refresh_token +
                                     "&client_id=" + kClientId +
                                     "&grant_type=refresh_token";

    app_->application_context_->ConnectToEnvironmentService(
        network_service_.NewRequest());
    network_service_->CreateURLLoader(url_loader_.NewRequest());

    // This flow exlusively branches below, so we need to put it in a shared
    // container from which it can be removed once for all branches.
    FlowTokenHolder branch{flow};

    Post(request_body, url_loader_.get(), kGoogleOAuthTokenEndpoint,
         [this, branch] {
           std::unique_ptr<FlowToken> flow = branch.Continue();
           FTL_CHECK(flow);
           Success(*flow);
         },
         [this, branch](const std::string error_message) {
           std::unique_ptr<FlowToken> flow = branch.Continue();
           FTL_CHECK(flow);
           Failure(*flow, error_message);
         },
         [this](rapidjson::Document doc) {
           return GetShortLivedTokens(std::move(doc));
         });
  }

  // Read saved user's refresh token from Credential store. If account is not
  // found, an empty token is returned.
  std::string GetRefreshToken() {
    // TODO(ukode): Read from app_->creds_ if valid before parsing.
    const ::auth::CredentialStore* credentials_storage = ParseCredsFile();
    if (credentials_storage == nullptr) {
      FTL_LOG(ERROR) << "Failed to parse credentials.";
      return "";
    }

    for (const auto* credential : *credentials_storage->creds()) {
      if (credential->account_id()->str() == account_id_) {
        for (const auto* token : *credential->tokens()) {
          switch (token->identity_provider()) {
            case ::auth::IdentityProvider_GOOGLE:
              return token->refresh_token()->str();
            default:
              FTL_LOG(WARNING) << "Unrecognized IdentityProvider"
                               << token->identity_provider();
          }
        }
      }
    }
    return "";
  }

  // Parse access and id tokens from OAUth endpoints into local token in-memory
  // cache.
  bool GetShortLivedTokens(rapidjson::Document tokens) {
    if (!tokens.HasMember("access_token")) {
      FTL_LOG(ERROR) << "Tokens returned from server does not contain "
                     << "access_token. Returned token: "
                     << modular::JsonValueToPrettyString(tokens);
      return false;
    };

    if ((token_type_ == ID_TOKEN) && !tokens.HasMember("id_token")) {
      FTL_LOG(ERROR) << "Tokens returned from server does not contain "
                     << "id_token. Returned token: "
                     << modular::JsonValueToPrettyString(tokens);
      return false;
    }

    // Add the token generation timestamp to |tokens| for caching.
    uint64_t creation_ts = ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
    app_->oauth_tokens_[account_id_] = {
        creation_ts,
        tokens["expires_in"].GetUint64(),
        tokens["access_token"].GetString(),
        tokens["id_token"].GetString(),
        std::map<std::string, FirebaseAuthToken>(),
    };

    return true;
  }

  // Returns true if the access and idtokens stored in cache are expired.
  bool HasCacheExpired() {
    FTL_DCHECK(app_);
    FTL_DCHECK(!account_id_.empty());

    if (app_->oauth_tokens_.find(account_id_) == app_->oauth_tokens_.end()) {
      FTL_VLOG(1) << "Account: [" << account_id_ << "] not found in cache.";
      return false;
    }

    uint64_t current_ts = ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
    uint64_t creation_ts = app_->oauth_tokens_[account_id_].creation_ts;
    uint64_t token_expiry = app_->oauth_tokens_[account_id_].expires_in;
    if ((current_ts - creation_ts) < token_expiry) {
      FTL_VLOG(1) << "Returning access/id tokens for account [" << account_id_
                  << "] from cache. ";
      return true;
    }

    return false;
  }

  void Success(FlowToken flow) {
    if (app_->oauth_tokens_.find(account_id_) == app_->oauth_tokens_.end()) {
      // In guest mode, return empty tokens.
      result_ = "";
    } else {
      switch (token_type_) {
        case ACCESS_TOKEN:
          result_ = app_->oauth_tokens_[account_id_].access_token;
          break;
        case ID_TOKEN:
          result_ = app_->oauth_tokens_[account_id_].id_token;
          break;
        case FIREBASE_JWT_TOKEN:
        default:
          Failure(flow, "invalid token type");
      }
    }
  }

  void Failure(FlowToken flow, const std::string& error_message) {
    FTL_LOG(ERROR) << error_message;
  }

  const std::string account_id_;
  const std::string firebase_api_key_;
  TokenType token_type_;
  OAuthTokenManagerApp* const app_;

  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;

  fidl::String result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GoogleOAuthTokensCall);
};

// TODO(alhaad): Use variadic template in |Operation|. That way, parameters to
// |callback| can be returned as parameters to |Done()|.
class OAuthTokenManagerApp::GoogleUserCredsCall : Operation<>,
                                                  web_view::WebRequestDelegate {
 public:
  GoogleUserCredsCall(OperationContainer* const container,
                      AccountPtr account,
                      OAuthTokenManagerApp* const app,
                      const AddAccountCallback& callback)
      : Operation(container, [] {}),
        account_(std::move(account)),
        app_(app),
        callback_(callback) {
    Ready();
  }

 private:
  // |Operation|
  void Run() override {
    // No FlowToken used here; calling Done() directly is more suitable,
    // because of the flow of control through web_view::WebRequestDelegate.

    auto view_owner = SetupWebView();

    // Set a delegate which will parse incoming URLs for authorization code.
    // TODO(alhaad/ukode): We need to set a timout here in-case we do not get
    // the code.
    web_view::WebRequestDelegatePtr web_request_delegate;
    web_request_delegate_bindings_.AddBinding(
        this, web_request_delegate.NewRequest());
    web_view_->SetWebRequestDelegate(std::move(web_request_delegate));

    const std::vector<std::string> scopes(kScopes.begin(), kScopes.end());
    std::string joined_scopes = ftl::JoinStrings(scopes, "+");

    std::string url = kGoogleOAuthAuthEndpoint;
    url += "?scope=" + joined_scopes;
    url += "&response_type=code&redirect_uri=";
    url += kRedirectUri;
    url += "&client_id=";
    url += kClientId;

    web_view_->SetUrl(std::move(url));

    app_->account_provider_context_->GetAuthenticationContext(
        account_->id, auth_context_.NewRequest());

    auth_context_->StartOverlay(std::move(view_owner));
  }

  // |web_view::WebRequestDelegate|
  void WillSendRequest(const fidl::String& incoming_url) override {
    const std::string uri = incoming_url.get();
    const std::string prefix = std::string{kRedirectUri} + "?code=";
    auto pos = uri.find(prefix);
    if (pos != 0) {
      return;
    }

    auto code = uri.substr(prefix.size(), std::string::npos);
    // There is a '#' character at the end.
    code.pop_back();

    const std::string request_body =
        "code=" + code + "&redirect_uri=" + kRedirectUri +
        "&client_id=" + kClientId + "&grant_type=authorization_code";

    app_->application_context_->ConnectToEnvironmentService(
        network_service_.NewRequest());
    network_service_->CreateURLLoader(url_loader_.NewRequest());

    Post(request_body, url_loader_.get(), kGoogleOAuthTokenEndpoint,
         [this] { Success(); },
         [this](const std::string error_message) { Failure(error_message); },
         [this](rapidjson::Document doc) {
           return ProcessCredentials(std::move(doc));
         });
  }

  // Parses refresh tokens from auth endpoint response and persists it in
  // |kCredentialsFile|.
  bool ProcessCredentials(rapidjson::Document tokens) {
    if (!tokens.HasMember("refresh_token") ||
        !tokens.HasMember("access_token")) {
      FTL_LOG(ERROR) << "Tokens returned from server does not contain "
                     << "refresh_token or access_token. Returned token: "
                     << modular::JsonValueToPrettyString(tokens);
      return false;
    };

    if (!SaveCredentials(tokens["refresh_token"].GetString())) {
      return false;
    }

    // Store short lived tokens local in-memory cache.
    uint64_t creation_ts = ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
    app_->oauth_tokens_[account_->id] = {
        creation_ts,
        tokens["expires_in"].GetUint64(),
        tokens["access_token"].GetString(),
        tokens["id_token"].GetString(),
        std::map<std::string, FirebaseAuthToken>(),
    };
    return true;
  }

  // Saves new credentials to the persistent creds storage file.
  bool SaveCredentials(const std::string& refresh_token) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<::auth::UserCredential>> creds;

    // Reserialize existing users.
    if (app_->creds_ && app_->creds_->creds()) {
      for (const auto* cred : *app_->creds_->creds()) {
        if (cred->account_id()->str() == account_->id) {
          // Update existing credentials
          continue;
        }

        std::vector<flatbuffers::Offset<::auth::IdpCredential>> idp_creds;
        for (const auto* idp_cred : *cred->tokens()) {
          idp_creds.push_back(::auth::CreateIdpCredential(
              builder, idp_cred->identity_provider(),
              builder.CreateString(idp_cred->refresh_token())));
        }

        creds.push_back(::auth::CreateUserCredential(
            builder, builder.CreateString(cred->account_id()),
            builder.CreateVector<flatbuffers::Offset<::auth::IdpCredential>>(
                std::move(idp_creds))));
      }
    }

    // add the new credential for |account_->id|.
    std::vector<flatbuffers::Offset<::auth::IdpCredential>> new_idp_creds;
    new_idp_creds.push_back(::auth::CreateIdpCredential(
        builder, ::auth::IdentityProvider_GOOGLE,
        builder.CreateString(std::move(refresh_token))));

    creds.push_back(::auth::CreateUserCredential(
        builder, builder.CreateString(account_->id),
        builder.CreateVector<flatbuffers::Offset<::auth::IdpCredential>>(
            std::move(new_idp_creds))));

    builder.Finish(::auth::CreateCredentialStore(
        builder, builder.CreateVector(std::move(creds))));

    std::string new_serialized_creds = std::string(
        reinterpret_cast<const char*>(builder.GetCurrentBufferPointer()),
        builder.GetSize());

    // Add new credentials to in-memory cache |creds_|.
    app_->creds_ = ::auth::GetCredentialStore(new_serialized_creds.data());

    return WriteCredsFile(new_serialized_creds);
  }

  void Success() {
    callback_(std::move(account_), nullptr);
    auth_context_->StopOverlay();
    Done();
  }

  void Failure(const std::string& error_message) {
    FTL_LOG(ERROR) << error_message;
    callback_(nullptr, error_message);
    auth_context_->StopOverlay();
    Done();
  }

  mozart::ViewOwnerPtr SetupWebView() {
    app::ServiceProviderPtr web_view_services;
    auto web_view_launch_info = app::ApplicationLaunchInfo::New();
    web_view_launch_info->url = kWebViewUrl;
    web_view_launch_info->services = web_view_services.NewRequest();
    app_->application_context_->launcher()->CreateApplication(
        std::move(web_view_launch_info), web_view_controller_.NewRequest());
    web_view_controller_.set_connection_error_handler([this] {
      // web_view is not build by default because of the time it adds to the
      // build.
      // TODO(alhaad/ukode): Fallback to a pre-build version.
      FTL_CHECK(false)
          << "web_view not found at " << kWebViewUrl << ". "
          << "Please build web_view locally. Instructions at "
          << "https://fuchsia.googlesource.com/web_view/+/master/README.md";
    });

    mozart::ViewOwnerPtr view_owner;
    mozart::ViewProviderPtr view_provider;
    ConnectToService(web_view_services.get(), view_provider.NewRequest());
    app::ServiceProviderPtr web_view_moz_services;
    view_provider->CreateView(view_owner.NewRequest(),
                              web_view_moz_services.NewRequest());

    ConnectToService(web_view_moz_services.get(), web_view_.NewRequest());

    return view_owner;
  }

  AccountPtr account_;
  OAuthTokenManagerApp* const app_;
  const AddAccountCallback callback_;

  AuthenticationContextPtr auth_context_;

  web_view::WebViewPtr web_view_;
  app::ApplicationControllerPtr web_view_controller_;

  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;

  fidl::BindingSet<web_view::WebRequestDelegate> web_request_delegate_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GoogleUserCredsCall);
};

class OAuthTokenManagerApp::GoogleProfileAttributesCall : Operation<> {
 public:
  GoogleProfileAttributesCall(OperationContainer* const container,
                              AccountPtr account,
                              OAuthTokenManagerApp* const app,
                              const AddAccountCallback& callback)
      : Operation(container, [] {}),
        account_(std::move(account)),
        app_(app),
        callback_(callback) {
    Ready();
  }

 private:
  // |Operation|
  void Run() override {
    if (!account_) {
      Failure("Account is null.");
      return;
    }

    if (app_->oauth_tokens_.find(account_->id) == app_->oauth_tokens_.end()) {
      FTL_LOG(ERROR) << "Account: " << account_->id << " not found.";
      Success();  // Maybe a guest account.
      return;
    }

    const std::string access_token =
        app_->oauth_tokens_[account_->id].access_token;
    app_->application_context_->ConnectToEnvironmentService(
        network_service_.NewRequest());
    network_service_->CreateURLLoader(url_loader_.NewRequest());

    // Fetch profile atrributes for the provisioned user using
    // https://developers.google.com/+/web/api/rest/latest/people/get api.
    Get(url_loader_.get(), kGooglePeopleGetEndpoint, access_token,
        [this] { Success(); },
        [this](const std::string error_message) { Failure(error_message); },
        [this](rapidjson::Document doc) {
          return SetAccountAttributes(std::move(doc));
        });
  }

  // Populate profile urls and display name for the account.
  bool SetAccountAttributes(rapidjson::Document attributes) {
    FTL_VLOG(1) << "People:get api response: "
                << modular::JsonValueToPrettyString(attributes);

    if (!account_) {
      return false;
    }

    if (attributes.HasMember("displayName")) {
      account_->display_name = attributes["displayName"].GetString();
    } else {
      account_->display_name = "";
    }

    if (attributes.HasMember("url")) {
      account_->url = attributes["url"].GetString();
    } else {
      account_->url = "";
    }

    if (attributes.HasMember("image")) {
      account_->image_url = attributes["image"]["url"].GetString();
    } else {
      account_->image_url = "";
    }

    return true;
  }

  void Success() {
    callback_(std::move(account_), nullptr);
    Done();
  }

  void Failure(const std::string& error_message) {
    FTL_LOG(ERROR) << error_message;
    // Account is missing profile attributes, but still valid.
    callback_(std::move(account_), error_message);
    Done();
  }

  AccountPtr account_;
  OAuthTokenManagerApp* const app_;
  const AddAccountCallback callback_;

  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GoogleProfileAttributesCall);
};

OAuthTokenManagerApp::OAuthTokenManagerApp()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      binding_(this) {
  application_context_->outgoing_services()->AddService<AccountProvider>(
      [this](fidl::InterfaceRequest<AccountProvider> request) {
        binding_.Bind(std::move(request));
      });

  // Reserialize existing users.
  creds_ = ParseCredsFile();
  if (creds_ == nullptr) {
    FTL_LOG(WARNING) << "user credentials file either missing or corrupted: "
                     << kCredentialsFile;
  }
}

void OAuthTokenManagerApp::Initialize(
    fidl::InterfaceHandle<AccountProviderContext> provider) {
  FTL_VLOG(1) << "OAuthTokenManagerApp::Initialize()";
  account_provider_context_.Bind(std::move(provider));
}

// TODO(alhaad): Check if account id already exists.
std::string OAuthTokenManagerApp::GenerateAccountId() {
  uint32_t random_number;
  size_t random_size;
  mx_status_t status =
      mx_cprng_draw(&random_number, sizeof random_number, &random_size);
  FTL_CHECK(status == MX_OK);
  FTL_CHECK(sizeof random_number == random_size);
  return std::to_string(random_number);
}

void OAuthTokenManagerApp::AddAccount(IdentityProvider identity_provider,
                                      const AddAccountCallback& callback) {
  FTL_VLOG(1) << "OAuthTokenManagerApp::AddAccount()";
  auto account = auth::Account::New();

  account->id = GenerateAccountId();
  account->identity_provider = identity_provider;

  switch (identity_provider) {
    case IdentityProvider::DEV:
      callback(std::move(account), nullptr);
      return;
    case IdentityProvider::GOOGLE:
      new GoogleUserCredsCall(
          &operation_queue_, std::move(account), this,
          [this, callback](AccountPtr account, const fidl::String error_msg) {
            if (error_msg) {
              callback(nullptr, error_msg);
              return;
            }

            new GoogleProfileAttributesCall(&operation_queue_,
                                            std::move(account), this, callback);
          });
      return;
    default:
      callback(nullptr, "Unrecognized Identity Provider");
  }
}

void OAuthTokenManagerApp::GetTokenProviderFactory(
    const fidl::String& account_id,
    fidl::InterfaceRequest<TokenProviderFactory> request) {
  new TokenProviderFactoryImpl(account_id, this, std::move(request));
}

void OAuthTokenManagerApp::RefreshToken(
    const std::string& account_id,
    const TokenType& token_type,
    const std::function<void(std::string)>& callback) {
  FTL_VLOG(1) << "OAuthTokenManagerApp::RefreshToken()";
  new GoogleOAuthTokensCall(&operation_queue_, account_id, token_type, this,
                            callback);
}

void OAuthTokenManagerApp::RefreshFirebaseToken(
    const std::string& account_id,
    const std::string& firebase_api_key,
    const std::string& id_token,
    const FirebaseTokenCallback& callback) {
  FTL_VLOG(1) << "OAuthTokenManagerApp::RefreshFirebaseToken()";
  new GoogleFirebaseTokensCall(&operation_queue_, account_id, firebase_api_key,
                               id_token, this, callback);
}

}  // namespace auth
}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  modular::auth::OAuthTokenManagerApp app;
  loop.Run();
  return 0;
}
