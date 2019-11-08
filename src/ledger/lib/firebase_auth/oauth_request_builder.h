// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_OAUTH_REQUEST_BUILDER_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_OAUTH_REQUEST_BUILDER_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>

namespace firebase_auth {

// Request builder for an OAuth Https Request. This builder converts the
// oauth endpoint request to an URI in the format as described by the OAuth
// protocol specification:
//     - https://tools.ietf.org/html/rfc6749
class OAuthRequestBuilder {
 public:
  OAuthRequestBuilder(const std::string& url, const std::string& method);

  ~OAuthRequestBuilder();

  // Sets the HTTP request body to the json encoded string |body|. This method
  // also adds the relevant http headers for accept, content-type and
  // content-length fields for posting JSON data.
  OAuthRequestBuilder& SetJsonBody(const std::string& body);

  // Url encodes the query params which are appended to the url string while
  // building the request.
  OAuthRequestBuilder& SetQueryParams(std::map<std::string, std::string> query_params);

  // Returns an HTTP |URLRequest| handle for the OAuth endpoint.
  ::fuchsia::net::oldhttp::URLRequest Build() const;

 private:
  // Sets the HTTP request body field to |body|.
  OAuthRequestBuilder& SetRequestBody(const std::string& body);

  const std::string url_;
  const std::string method_;
  std::string query_string_;
  std::string request_body_;
  std::map<std::string, std::string> http_headers_;
};

// Request builder for an Https Request to the Firebase sign in endpoint.
class FirebaseRequestBuilder {
 public:
  FirebaseRequestBuilder(std::string firebase_api_key, std::string google_id_token);

  ~FirebaseRequestBuilder();

  // Returns an HTTP |URLRequest| handle for the OAuth endpoint.
  ::fuchsia::net::oldhttp::URLRequest Build() const;

 private:
  OAuthRequestBuilder oauth_request_;
};

}  // namespace firebase_auth

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_OAUTH_REQUEST_BUILDER_H_
