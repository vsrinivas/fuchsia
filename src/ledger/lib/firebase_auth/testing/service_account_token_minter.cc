// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/service_account_token_minter.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <time.h>

#include <openssl/bio.h>
#include <openssl/digest.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "peridot/lib/base64url/base64url.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/json_parser/rapidjson_validation.h"

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

namespace {
constexpr fxl::StringView kIdentityResponseSchema = R"({
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

rapidjson::SchemaDocument& GetResponseSchema() {
  static auto schema = json_parser::InitSchema(kIdentityResponseSchema);
  FXL_DCHECK(schema);
  return *schema;
}

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

}  // namespace

struct ServiceAccountTokenMinter::CachedToken {
  std::string id_token;
  // TODO: Use zx::time for expiration_time.
  time_t expiration_time;
};

ServiceAccountTokenMinter::GetTokenResponse ServiceAccountTokenMinter::GetErrorResponse(
    Status status, const std::string& error_msg) {
  GetTokenResponse response = {status, /* response status */
                               "",     /* id_token */
                               "",     /* local_id */
                               "",     /* email */
                               error_msg /* detailed error msg */};
  return response;
}

ServiceAccountTokenMinter::GetTokenResponse ServiceAccountTokenMinter::GetSuccessResponse(
    const std::string& id_token) {
  GetTokenResponse response = {Status::OK,                /* success status */
                               id_token,                  /* token */
                               user_id_,                  /* local_id */
                               user_id_ + "@example.com", /* email */
                               "OK" /* success msg */};
  return response;
}

ServiceAccountTokenMinter::ServiceAccountTokenMinter(
    async_dispatcher_t* dispatcher, network_wrapper::NetworkWrapper* network_wrapper,
    std::unique_ptr<Credentials> credentials, std::string user_id)
    : network_wrapper_(network_wrapper),
      credentials_(std::move(credentials)),
      user_id_(std::move(user_id)),
      in_progress_requests_(dispatcher) {}

ServiceAccountTokenMinter::~ServiceAccountTokenMinter() {
  for (const auto& pair : in_progress_callbacks_) {
    ResolveCallbacks(pair.first,
                     GetErrorResponse(Status::INTERNAL_ERROR,
                                      "Account provider deleted with requests in flight."));
  }
}

void ServiceAccountTokenMinter::GetFirebaseToken(fidl::StringPtr firebase_api_key,
                                                 GetFirebaseTokenCallback callback) {
  // A request is in progress to get a token. Registers the callback that will
  // be called when the request ends.
  if (!in_progress_callbacks_[firebase_api_key.value_or("")].empty()) {
    in_progress_callbacks_[firebase_api_key.value_or("")].push_back(std::move(callback));
    return;
  }

  // Check if a token is currently cached.
  if (cached_tokens_[firebase_api_key.value_or("")]) {
    auto& cached_token = cached_tokens_[firebase_api_key.value_or("")];
    if (time(nullptr) < cached_token->expiration_time) {
      callback(GetSuccessResponse(cached_token->id_token));
      return;
    }

    // The token expired. Falls back to fetch a new one.
    cached_tokens_.erase(firebase_api_key.value_or(""));
  }

  // Build the custom token to exchange for an id token.
  std::string custom_token;
  if (!GetCustomToken(&custom_token)) {
    callback(
        GetErrorResponse(Status::INTERNAL_ERROR, "Unable to compute custom authentication token."));
    return;
  }

  in_progress_callbacks_[firebase_api_key.value_or("")].push_back(std::move(callback));

  in_progress_requests_.emplace(network_wrapper_->Request(
      [this, firebase_api_key = firebase_api_key.value_or(""),
       custom_token = std::move(custom_token)] {
        return GetIdentityRequest(firebase_api_key, custom_token);
      },
      [this, firebase_api_key = firebase_api_key.value_or("")](http::URLResponse response) {
        HandleIdentityResponse(firebase_api_key, std::move(response));
      }));
}

std::string ServiceAccountTokenMinter::GetClientId() { return credentials_->client_id(); }

std::string ServiceAccountTokenMinter::GetClaims() {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key("iss");
  writer.String(credentials_->client_email());

  writer.Key("sub");
  writer.String(credentials_->client_email());

  writer.Key("aud");
  writer.String(
      "https://identitytoolkit.googleapis.com/"
      "google.identity.identitytoolkit.v1.IdentityToolkit");

  time_t current_time = time(nullptr);
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

bool ServiceAccountTokenMinter::GetCustomToken(std::string* custom_token) {
  std::string message = GetHeader() + "." + GetClaims();

  bssl::ScopedEVP_MD_CTX md_ctx;
  if (EVP_DigestSignInit(md_ctx.get(), nullptr, EVP_sha256(), nullptr,
                         credentials_->private_key().get()) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  if (EVP_DigestSignUpdate(md_ctx.get(), message.c_str(), message.size()) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  size_t result_length;
  if (EVP_DigestSignFinal(md_ctx.get(), nullptr, &result_length) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  char result[result_length];
  if (EVP_DigestSignFinal(md_ctx.get(), reinterpret_cast<uint8_t*>(result), &result_length) != 1) {
    FXL_LOG(ERROR) << ERR_reason_error_string(ERR_get_error());
    return false;
  }

  std::string signature = base64url::Base64UrlEncode(fxl::StringView(result, result_length));

  *custom_token = message + "." + signature;
  return true;
}

http::URLRequest ServiceAccountTokenMinter::GetIdentityRequest(const std::string& api_key,
                                                               const std::string& custom_token) {
  http::URLRequest request;
  request.url =
      "https://www.googleapis.com/identitytoolkit/v3/relyingparty/"
      "verifyCustomToken?key=" +
      api_key;
  request.method = "POST";
  request.auto_follow_redirects = true;
  request.response_body_mode = http::ResponseBodyMode::BUFFER;
  request.headers.emplace();

  // content-type header.
  http::HttpHeader content_type_header;
  content_type_header.name = "content-type";
  content_type_header.value = "application/json";
  request.headers->push_back(std::move(content_type_header));

  // set accept header
  http::HttpHeader accept_header;
  accept_header.name = "accept";
  accept_header.value = "application/json";
  request.headers->push_back(std::move(accept_header));

  fsl::SizedVmo data;
  bool result = fsl::VmoFromString(GetIdentityRequestBody(custom_token), &data);
  FXL_DCHECK(result);

  request.body = http::URLBody::New();
  request.body->set_buffer(std::move(data).ToTransport());

  return request;
}

std::string ServiceAccountTokenMinter::GetIdentityRequestBody(const std::string& custom_token) {
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

void ServiceAccountTokenMinter::HandleIdentityResponse(const std::string& api_key,
                                                       http::URLResponse response) {
  if (response.error) {
    ResolveCallbacks(
        api_key, GetErrorResponse(Status::NETWORK_ERROR, response.error->description.value_or("")));
    return;
  }

  std::string response_body;
  if (response.body) {
    FXL_DCHECK(response.body->is_buffer());
    if (!fsl::StringFromVmo(response.body->buffer(), &response_body)) {
      ResolveCallbacks(api_key,
                       GetErrorResponse(Status::INTERNAL_ERROR, "Unable to read from VMO."));
      return;
    }
  }

  if (response.status_code != 200) {
    ResolveCallbacks(api_key, GetErrorResponse(Status::AUTH_SERVER_ERROR, response_body));
    return;
  }

  rapidjson::Document document;
  document.Parse(response_body.c_str(), response_body.size());
  if (document.HasParseError() || !document.IsObject()) {
    ResolveCallbacks(api_key, GetErrorResponse(Status::BAD_RESPONSE,
                                               "Unable to parse response: " + response_body));
    return;
  }

  if (!json_parser::ValidateSchema(document, GetResponseSchema(), "identity response")) {
    ResolveCallbacks(
        api_key, GetErrorResponse(Status::BAD_RESPONSE, "Malformed response: " + response_body));
    return;
  }

  auto cached_token = std::make_unique<CachedToken>();
  cached_token->id_token = convert::ToString(document["idToken"]);
  cached_token->expiration_time =
      time(nullptr) +
      (9u * fxl::StringToNumber<time_t>(convert::ToStringView(document["expiresIn"])) / 10u);
  const auto& id_token = cached_token->id_token;
  cached_tokens_[api_key] = std::move(cached_token);
  ResolveCallbacks(api_key, GetSuccessResponse(id_token));
}

void ServiceAccountTokenMinter::ResolveCallbacks(const std::string& api_key,
                                                 GetTokenResponse response) {
  auto callbacks = std::move(in_progress_callbacks_[api_key]);
  in_progress_callbacks_[api_key].clear();
  for (const auto& callback : callbacks) {
    callback(response);
  }
}

}  // namespace service_account
