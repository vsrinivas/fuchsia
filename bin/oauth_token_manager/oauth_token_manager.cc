// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// OAuthTokenManagerApp is a simple auth service hack for fetching user OAuth
// tokens to talk programmatically to backend apis. These apis are hosted or
// integrated with Identity providers such as Google, Twitter, Spotify etc.

#include <iomanip>
#include <iostream>
#include <memory>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/auth/account_provider.fidl.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/web_runner/services/web_view.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/join_strings.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/synchronization/sleep.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {
namespace auth {

namespace {

// TODO(alhaad/ukode): Move the following to a configuration file.
// NOTE: We are currently using a single client-id in Fuchsia. This is temporary
// and will change in the future.
constexpr char kClientId[] =
    "1051596886047-kjfjv6tuoluj61n5cedv71ansrj3ggi8.apps."
    "googleusercontent.com";
constexpr char kGoogleOAuthEndpoint[] =
    "https://accounts.google.com/o/oauth2/v2/auth";
constexpr char kRedirectUri[] = "com.google.fuchsia.auth:/oauth2redirect";
constexpr char kCredentialsFile[] = "/data/modular/device/v1/creds.db";
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

// Exactly one of success_callback and failure_callback is ever invoked.
void Post(const std::string& request_body,
          network::URLLoader* const url_loader,
          const std::function<void()>& success_callback,
          const std::function<void(std::string)>& failure_callback,
          const std::function<bool(rapidjson::Document)>& set_token_callback) {
  auto encoded_request_body = UrlEncode(request_body);

  mx::vmo data;
  auto result = mtl::VmoFromString(encoded_request_body, &data);
  FTL_DCHECK(result);

  network::URLRequestPtr request(network::URLRequest::New());
  request->url = "https://www.googleapis.com/oauth2/v4/token";
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
  content_type_header->value = "application/x-www-form-urlencoded";
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

    if (response->status_code != 200) {
      failure_callback("Status code: " + std::to_string(response->status_code) +
                       " while fetching access token.");
      return;
    }

    if (!response->body.is_null()) {
      FTL_DCHECK(response->body->is_stream());
      std::string response_body;
      // TODO(alhaad/ukode): Use non-blocking variant.
      if (!mtl::BlockingCopyToString(std::move(response->body->get_stream()),
                                     &response_body)) {
        failure_callback("Failed to read from socket.");
        return;
      }

      rapidjson::Document doc;
      doc.Parse(response_body);
      FTL_DCHECK(!doc.HasParseError());
      FTL_DCHECK(set_token_callback(std::move(doc)));
      success_callback();
    }
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
                  const fidl::String& display_name,
                  const AddAccountCallback& callback) override;

  // |AccountProvider|
  void GetTokenProviderFactory(
      const fidl::String& account_id,
      fidl::InterfaceRequest<TokenProviderFactory> request) override;

  // Generate a random account id.
  std::string GenerateAccountId();

  void RefreshToken(const std::string& account_id,
                    bool refresh_id_token,
                    const std::function<void(std::string)>& callback);

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
  rapidjson::Document user_creds_;

  // In-memory cache for short lived oauth tokens that resets on system reboots.
  // Tokens are cached based on the expiration time set by the Identity
  // provider.
  // TODO(ukode): Replace rapidjson with a dedicated data structure for storing
  // oauth tokens.
  rapidjson::Document oauth_tokens_;

  // We are using operations here not to guard state across asynchronous calls
  // but rather to clean up state after an 'operation' is done.
  OperationCollection operation_collection_;

  class GoogleOAuthTokensCall;
  class GoogleUserCredsCall;

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
    if (!app_->user_creds_.IsObject() ||
        !app_->user_creds_.HasMember(account_id_)) {
      FTL_VLOG(1) << "User not found";
      callback(nullptr);
      return;
    }
    app_->RefreshToken(account_id_, false, callback);
  }

  // |TokenProvider|
  void GetIdToken(const GetIdTokenCallback& callback) override {
    if (!app_->user_creds_.IsObject() ||
        !app_->user_creds_.HasMember(account_id_)) {
      FTL_VLOG(1) << "User not found";
      callback(nullptr);
      return;
    }

    app_->RefreshToken(account_id_, true, callback);
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

class OAuthTokenManagerApp::GoogleOAuthTokensCall : Operation<fidl::String> {
 public:
  GoogleOAuthTokensCall(OperationContainer* const container,
                        const std::string& account_id,
                        const bool refresh_id_token,
                        OAuthTokenManagerApp* const app,
                        const std::function<void(fidl::String)>& callback)
      : Operation(container, callback),
        account_id_(account_id),
        refresh_id_token_(refresh_id_token),
        app_(app) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    FTL_LOG(1) << "Account_ID:" << account_id_;
    const rapidjson::Value& creds = app_->user_creds_[account_id_];
    FTL_DCHECK(creds.IsObject());
    FTL_DCHECK(creds.HasMember("refresh_token"));
    const std::string refresh_token = creds["refresh_token"].GetString();

    if (refresh_token.empty()) {
      Failure(flow, "User not found");
      return;
    }

    // Check for existing expiry time before fetching it from server.
    if (app_->oauth_tokens_.HasMember(account_id_)) {
      uint64_t current_ts = ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
      uint64_t creation_ts =
          app_->oauth_tokens_[account_id_]["creation_ts"].GetUint64();
      uint64_t token_expiry =
          app_->oauth_tokens_[account_id_]["expires_in"].GetUint64();

      if ((current_ts - creation_ts) < token_expiry) {
        // access/id token is still valid.
        Success(flow);
        return;
      }
    }

    // Existing tokens expired, exchange refresh token for fresh access and
    // id tokens.
    const std::string request_body = "refresh_token=" + refresh_token +
                                     "&client_id=" + kClientId +
                                     "&grant_type=refresh_token";

    app_->application_context_->ConnectToEnvironmentService(
        network_service_.NewRequest());
    network_service_->CreateURLLoader(url_loader_.NewRequest());

    // This flow exlusively branches below, so we need to put it in a shared
    // container from which it can be removed once for all branches.
    FlowTokenHolder branch{flow};

    Post(request_body, url_loader_.get(),
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
         [this](rapidjson::Document doc) { return GetTokens(std::move(doc)); });
  }

  // Parses access and idtokens from token endpoint response and saves it to
  // local token in-memory cache.
  bool GetTokens(rapidjson::Document tokens) {
    if (!tokens.HasMember("access_token")) {
      FTL_DCHECK(false) << "Tokens returned from server does not contain "
                        << "access_token. Returned token: "
                        << modular::JsonValueToPrettyString(tokens);
      return false;
    };

    if (refresh_id_token_ && !tokens.HasMember("id_token")) {
      FTL_DCHECK(false) << "Tokens returned from server does not contain "
                        << "id_token. Returned token: "
                        << modular::JsonValueToPrettyString(tokens);
      return false;
    }

    auto& allocator = app_->oauth_tokens_.GetAllocator();

    if (app_->oauth_tokens_.HasMember(account_id_)) {
      app_->oauth_tokens_.RemoveMember(account_id_);
    }

    // Add the token generation timestamp to |tokens| for caching.
    std::string ts_key_name("creation_ts");
    rapidjson::Value creation_ts_key(ts_key_name.c_str(), ts_key_name.size(),
                                     tokens.GetAllocator());
    uint64_t creation_ts = ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
    tokens.AddMember(creation_ts_key, creation_ts, tokens.GetAllocator());

    app_->oauth_tokens_.AddMember(rapidjson::Value(account_id_, allocator),
                                  tokens, allocator);

    return true;
  }

  void Success(FlowToken flow) {
    if (!refresh_id_token_) {
      result_ = app_->oauth_tokens_[account_id_]["access_token"].GetString();
    } else {
      result_ = app_->oauth_tokens_[account_id_]["id_token"].GetString();
    }
  }

  void Failure(FlowToken flow, const std::string& error_message) {
    FTL_LOG(ERROR) << error_message;
  }

  const std::string account_id_;
  const bool refresh_id_token_;
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
    // No FlowToken used here; calling Done() directly is more suitable, because
    // of the flow of control through web_view::WebRequestDelegate.

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

    std::string url = kGoogleOAuthEndpoint;
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

    Post(request_body, url_loader_.get(), [this] { Success(); },
         [this](const std::string error_message) { Failure(error_message); },
         [this](rapidjson::Document doc) {
           return SaveCredentials(std::move(doc));
         });
  }

  // Parses refresh tokens from auth endpoint response and persists it in
  // |kCredentialsFile|.
  bool SaveCredentials(rapidjson::Document tokens) {
    if (!tokens.HasMember("refresh_token")) {
      FTL_DCHECK(false) << "Tokens returned from server does not contain "
                        << "refresh_token. Returned token: "
                        << modular::JsonValueToPrettyString(tokens);
      return false;
    };

    // Keep only persistent refresh tokens in this db, other short lived tokens
    // are saved only in in-memory cache.
    if (tokens.HasMember("access_token")) {
      tokens.RemoveMember("access_token");
    };

    if (tokens.HasMember("id_token")) {
      tokens.RemoveMember("id_token");
    };

    auto& account_id = account_->id;
    auto& allocator = app_->user_creds_.GetAllocator();

    // Remove any existing tokens for this account_id.
    if (app_->user_creds_.HasMember(account_id)) {
      app_->user_creds_.RemoveMember(account_id);
    }

    app_->user_creds_.AddMember(rapidjson::Value(account_id, allocator), tokens,
                                allocator);

    // Save tokens to disk.
    if (!files::CreateDirectory(files::GetDirectoryName(kCredentialsFile))) {
      FTL_DCHECK(false) << "Unable to create directory for " <<
                        kCredentialsFile;
      return false;
    }
    auto serialized_tokens = modular::JsonValueToString(app_->user_creds_);
    FTL_VLOG(1) << "Tokens:" << serialized_tokens.data();

    if (!files::WriteFile(kCredentialsFile, serialized_tokens.data(),
                          serialized_tokens.size())) {
      FTL_DCHECK(false) << "Unable to write file " << kCredentialsFile;
      return false;
    }

    return true;
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
    web_view_controller_.set_connection_error_handler(
        [this] {
          // web_view is not build by default because of the time it adds to the
          // build.
          // TODO(alhaad/ukode): Fallback to a pre-build version.
          FTL_CHECK(false) << "web_view not found at " << kWebViewUrl << ". "
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

OAuthTokenManagerApp::OAuthTokenManagerApp()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      binding_(this) {
  application_context_->outgoing_services()->AddService<AccountProvider>(
      [this](fidl::InterfaceRequest<AccountProvider> request) {
        binding_.Bind(std::move(request));
      });

  if (files::IsFile(kCredentialsFile)) {
    std::string serialized_tokens;
    if (!files::ReadFileToString(kCredentialsFile, &serialized_tokens)) {
      FTL_DCHECK(false) << "Unable to read file " << kCredentialsFile;
    }
    user_creds_.Parse(serialized_tokens);
    if (!user_creds_.HasParseError() || !user_creds_.IsObject()) {
      FTL_LOG(WARNING) << "Credentials file got corrupted, " <<
                       "previous user records are lost.";
      user_creds_.SetObject();
    }
  } else {
    // Create an empty DOM.
    user_creds_.SetObject();
  }

  // Create an empty DOM for token cache.
  oauth_tokens_.SetObject();
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
  FTL_CHECK(status == NO_ERROR);
  FTL_CHECK(sizeof random_number == random_size);
  return std::to_string(random_number);
}

void OAuthTokenManagerApp::AddAccount(IdentityProvider identity_provider,
                                      const fidl::String& display_name,
                                      const AddAccountCallback& callback) {
  FTL_VLOG(1) << "OAuthTokenManagerApp::AddAccount()";
  auto account = auth::Account::New();

  account->id = GenerateAccountId();
  account->identity_provider = identity_provider;
  // TODO(alhaad/ukode): Derive |display_name| from user profile instead.
  account->display_name = display_name;

  switch (identity_provider) {
    case IdentityProvider::DEV:
      callback(std::move(account), nullptr);
      return;
    case IdentityProvider::GOOGLE:
      new GoogleUserCredsCall(&operation_collection_, std::move(account), this,
                              callback);
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
    const bool refresh_id_token,
    const std::function<void(std::string)>& callback) {
  FTL_VLOG(1) << "OAuthTokenManagerApp::RefreshToken()";
  new GoogleOAuthTokensCall(&operation_collection_, account_id,
                            refresh_id_token, this, callback);
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
