// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/service_account_token_provider.h"

#include <time.h>

#include <openssl/bio.h>
#include <openssl/digest.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/lib/base64url/base64url.h"
#include "peridot/lib/convert/convert.h"

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

namespace {
const char kServiceAccountConfigurationSchema[] = R"({
  "type": "object",
  "additionalProperties": true,
  "properties": {
    "private_key": {
      "type": "string"
    },
    "client_email": {
      "type": "string"
    },
    "client_id": {
      "type": "string"
    }
  },
  "required": ["private_key", "client_email", "client_id"]
})";

const char kIdentityResponseSchema[] = R"({
  "type": "object",
  "additionalProperties": true,
  "properties": {
    "idToken": {
      "type": "string"
    },
    "expiresIn": {
      "type": "string"
    }
  },
  "required": ["idToken", "expiresIn"]
})";

std::string GetHeader() {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key("typ");
  writer.String("JWT");

  writer.Key("alg");
  writer.String("RS256");

  writer.EndObject();

  return base64url::Base64UrlEncode(
      fxl::StringView(string_buffer.GetString(), string_buffer.GetSize()));
}

fuchsia::modular::auth::AuthErr GetError(fuchsia::modular::auth::Status status,
                               std::string message) {
  fuchsia::modular::auth::AuthErr error;
  error.status = status;
  error.message = message;
  return error;
}

std::unique_ptr<rapidjson::SchemaDocument> InitSchema(const char schemaSpec[]) {
  rapidjson::Document schema_document;
  if (schema_document.Parse(schemaSpec).HasParseError()) {
    FXL_DCHECK(false) << "Schema validation spec itself is not valid JSON.";
  }
  return std::make_unique<rapidjson::SchemaDocument>(schema_document);
}

bool ValidateSchema(const rapidjson::Value& value,
                    const rapidjson::SchemaDocument& schema) {
  rapidjson::SchemaValidator validator(schema);
  if (!value.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    FXL_LOG(ERROR) << "Incorrect schema at " << uri_buffer.GetString()
                   << " , schema violation: "
                   << validator.GetInvalidSchemaKeyword();
    return false;
  }
  return true;
}

}  // namespace

struct ServiceAccountTokenProvider::Credentials {
  std::string client_email;
  std::string client_id;

  bssl::UniquePtr<EVP_PKEY> private_key;

  std::unique_ptr<rapidjson::SchemaDocument> response_schema;
};

struct ServiceAccountTokenProvider::CachedToken {
  std::string id_token;
  time_t expiration_time;
};

ServiceAccountTokenProvider::ServiceAccountTokenProvider(
    network_wrapper::NetworkWrapper* network_wrapper, std::string user_id)
    : network_wrapper_(network_wrapper), user_id_(std::move(user_id)) {}

ServiceAccountTokenProvider::~ServiceAccountTokenProvider() {
  for (const auto& pair : in_progress_callbacks_) {
    ResolveCallbacks(
        pair.first, nullptr,
        GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR,
                 "Account provider deleted with requests in flight."));
  }
}

bool ServiceAccountTokenProvider::LoadCredentials(
    const std::string& json_file) {
  std::string file_content;
  if (!files::ReadFileToString(json_file, &file_content)) {
    FXL_LOG(ERROR) << "Unable to read file at: " << json_file;
    return false;
  }

  rapidjson::Document document;
  document.Parse(file_content.c_str(), file_content.size());
  if (document.HasParseError() || !document.IsObject()) {
    FXL_LOG(ERROR) << "Json file is incorrect at: " << json_file;
    return false;
  }

  auto service_account_schema = InitSchema(kServiceAccountConfigurationSchema);
  if (!ValidateSchema(document, *service_account_schema)) {
    return false;
  }

  credentials_ = std::make_unique<Credentials>();
  credentials_->client_email = document["client_email"].GetString();
  credentials_->client_id = document["client_id"].GetString();

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(document["private_key"].GetString(),
                      document["private_key"].GetStringLength()));
  credentials_->private_key.reset(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));

  if (EVP_PKEY_id(credentials_->private_key.get()) != EVP_PKEY_RSA) {
    FXL_LOG(ERROR) << "Provided key is not a RSA key.";
    return false;
  }

  credentials_->response_schema = InitSchema(kIdentityResponseSchema);

  return true;
}

void ServiceAccountTokenProvider::GetAccessToken(
    GetAccessTokenCallback callback) {
  FXL_NOTIMPLEMENTED();
  callback(nullptr,
           GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR, "Not implemented."));
}

void ServiceAccountTokenProvider::GetIdToken(GetIdTokenCallback callback) {
  FXL_NOTIMPLEMENTED();
  callback(nullptr,
           GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR, "Not implemented."));
}

void ServiceAccountTokenProvider::GetFirebaseAuthToken(
    fidl::StringPtr firebase_api_key, GetFirebaseAuthTokenCallback callback) {
  // A request is in progress to get a token. Registers the callback that will
  // be called when the request ends.
  if (!in_progress_callbacks_[firebase_api_key].empty()) {
    in_progress_callbacks_[firebase_api_key].push_back(callback);
    return;
  }

  // Check if a token is currently cached.
  if (cached_tokens_[firebase_api_key]) {
    auto& cached_token = cached_tokens_[firebase_api_key];
    if (time(nullptr) < cached_token->expiration_time) {
      callback(GetFirebaseToken(cached_token->id_token),
               GetError(fuchsia::modular::auth::Status::OK, "OK"));
      return;
    }

    // The token expired. Falls back to fetch a new one.
    cached_tokens_.erase(firebase_api_key);
  }

  // Build the custom token to exchange for an id token.
  std::string custom_token;
  if (!GetCustomToken(&custom_token)) {
    callback(GetFirebaseToken(nullptr),
             GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR,
                      "Unable to compute custom authentication token."));
  }

  in_progress_callbacks_[firebase_api_key].push_back(callback);

  in_progress_requests_.emplace(network_wrapper_->Request(
      [this, firebase_api_key = firebase_api_key.get(),
       custom_token = std::move(custom_token)] {
        return GetIdentityRequest(firebase_api_key, custom_token);
      },
      [this,
       firebase_api_key = firebase_api_key.get()](http::URLResponse response) {
        HandleIdentityResponse(firebase_api_key, std::move(response));
      }));
}

void ServiceAccountTokenProvider::GetClientId(GetClientIdCallback callback) {
  callback(credentials_->client_id);
}

std::string ServiceAccountTokenProvider::GetClaims() {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key("iss");
  writer.String(credentials_->client_email);

  writer.Key("sub");
  writer.String(credentials_->client_email);

  writer.Key("aud");
  writer.String(
      "https://identitytoolkit.googleapis.com/"
      "google.identity.identitytoolkit.v1.IdentityToolkit");

  time_t current_time = time(nullptr);
  // current_time = 1500471342;
  writer.Key("iat");
  writer.Int(current_time);

  writer.Key("exp");
  writer.Int(current_time + 3600);

  writer.Key("uid");
  writer.String(user_id_);

  writer.EndObject();

  return base64url::Base64UrlEncode(
      fxl::StringView(string_buffer.GetString(), string_buffer.GetSize()));
}

bool ServiceAccountTokenProvider::GetCustomToken(std::string* custom_token) {
  std::string message = GetHeader() + "." + GetClaims();

  bssl::ScopedEVP_MD_CTX md_ctx;
  if (EVP_DigestSignInit(md_ctx.get(), nullptr, EVP_sha256(), nullptr,
                         credentials_->private_key.get()) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  if (EVP_DigestSignUpdate(md_ctx.get(), message.c_str(), message.size()) !=
      1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  size_t result_length;
  if (EVP_DigestSignFinal(md_ctx.get(), nullptr, &result_length) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  char result[result_length];
  if (EVP_DigestSignFinal(md_ctx.get(), reinterpret_cast<uint8_t*>(result),
                          &result_length) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  std::string signature =
      base64url::Base64UrlEncode(fxl::StringView(result, result_length));

  *custom_token = message + "." + signature;
  return true;
}

fuchsia::modular::auth::FirebaseTokenPtr ServiceAccountTokenProvider::GetFirebaseToken(
    const std::string& id_token) {
  auto token = fuchsia::modular::auth::FirebaseToken::New();
  token->id_token = id_token;
  token->local_id = user_id_;
  token->email = user_id_ + "@example.com";
  return token;
}

http::URLRequest ServiceAccountTokenProvider::GetIdentityRequest(
    const std::string& api_key, const std::string& custom_token) {
  http::URLRequest request;
  request.url =
      "https://www.googleapis.com/identitytoolkit/v3/relyingparty/"
      "verifyCustomToken?key=" +
      api_key;
  request.method = "POST";
  request.auto_follow_redirects = true;
  request.response_body_mode = http::ResponseBodyMode::SIZED_BUFFER;

  // content-type header.
  http::HttpHeader content_type_header;
  content_type_header.name = "content-type";
  content_type_header.value = "application/json";
  request.headers.push_back(std::move(content_type_header));

  // set accept header
  http::HttpHeader accept_header;
  accept_header.name = "accept";
  accept_header.value = "application/json";
  request.headers.push_back(std::move(accept_header));

  fsl::SizedVmo data;
  bool result = fsl::VmoFromString(GetIdentityRequestBody(custom_token), &data);
  FXL_DCHECK(result);

  request.body = http::URLBody::New();
  request.body->set_sized_buffer(std::move(data).ToTransport());

  return request;
}

std::string ServiceAccountTokenProvider::GetIdentityRequestBody(
    const std::string& custom_token) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key("token");
  writer.String(custom_token);

  writer.Key("returnSecureToken");
  writer.Bool(true);

  writer.EndObject();

  return std::string(string_buffer.GetString(), string_buffer.GetSize());
}

void ServiceAccountTokenProvider::HandleIdentityResponse(
    const std::string& api_key, http::URLResponse response) {
  if (response.error) {
    ResolveCallbacks(api_key, nullptr,
                     GetError(fuchsia::modular::auth::Status::NETWORK_ERROR,
                              response.error->description));
    return;
  }

  std::string response_body;
  if (response.body) {
    FXL_DCHECK(response.body->is_sized_buffer());
    if (!fsl::StringFromVmo(response.body->sized_buffer(), &response_body)) {
      ResolveCallbacks(api_key, nullptr,
                       GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR,
                                "Unable to read from VMO."));
      return;
    }
  }

  if (response.status_code != 200) {
    ResolveCallbacks(
        api_key, nullptr,
        GetError(fuchsia::modular::auth::Status::OAUTH_SERVER_ERROR, response_body));
    return;
  }

  rapidjson::Document document;
  document.Parse(response_body.c_str(), response_body.size());
  if (document.HasParseError() || !document.IsObject()) {
    ResolveCallbacks(api_key, nullptr,
                     GetError(fuchsia::modular::auth::Status::BAD_RESPONSE,
                              "Unable to parse response: " + response_body));
    return;
  }

  if (!ValidateSchema(document, *credentials_->response_schema)) {
    ResolveCallbacks(api_key, nullptr,
                     GetError(fuchsia::modular::auth::Status::BAD_RESPONSE,
                              "Malformed response: " + response_body));
    return;
  }

  auto cached_token = std::make_unique<CachedToken>();
  cached_token->id_token = convert::ToString(document["idToken"]);
  cached_token->expiration_time =
      time(nullptr) + (9u *
                       fxl::StringToNumber<time_t>(
                           convert::ToStringView(document["expiresIn"])) /
                       10u);
  const auto& id_token = cached_token->id_token;
  cached_tokens_[api_key] = std::move(cached_token);
  ResolveCallbacks(api_key, GetFirebaseToken(id_token),
                   GetError(fuchsia::modular::auth::Status::OK, "OK"));
}

void ServiceAccountTokenProvider::ResolveCallbacks(
    const std::string& api_key, fuchsia::modular::auth::FirebaseTokenPtr token,
    fuchsia::modular::auth::AuthErr error) {
  auto callbacks = std::move(in_progress_callbacks_[api_key]);
  in_progress_callbacks_[api_key].clear();
  for (const auto& callback : callbacks) {
    callback(fidl::Clone(token), fidl::Clone(error));
  }
}

}  // namespace service_account
