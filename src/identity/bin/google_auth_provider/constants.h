// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <initializer_list>

namespace google_auth_provider {

constexpr char kFuchsiaClientId[] =
    "934259141868-rejmm4ollj1bs7th1vg2ur6antpbug79.apps.googleusercontent.com";
constexpr char kGoogleOAuthAuthEndpoint[] =
    "https://accounts.google.com/o/oauth2/v2/auth";
constexpr char kGoogleFuchsiaEndpoint[] =
    "https://accounts.google.com/embedded/setup/fuchsia";
constexpr char kGoogleOAuthTokenEndpoint[] =
    "https://www.googleapis.com/oauth2/v4/token";
constexpr char kGoogleRevokeTokenEndpoint[] =
    "https://accounts.google.com/o/oauth2/revoke";
constexpr char kGoogleUserInfoEndpoint[] =
    "https://www.googleapis.com/oauth2/v3/userinfo";
constexpr char kFirebaseAuthEndpoint[] =
    "https://www.googleapis.com/identitytoolkit/v3/relyingparty/"
    "verifyAssertion";
constexpr char kRedirectUri[] = "https://localhost/fuchsiaoauth2redirect";
constexpr char kWebViewUrl[] = "web_view";

constexpr auto kScopes = {
    // Used by google_auth_provider for retrieving unique user profile id.
    "openid",
    // Used by a variety of client components.
    "email",
    // Used by google_auth_provider for retrieving user profile attributes,
    // specifically display name, profile url, and profile image.
    "profile",
    // Used by components outside this repository.
    "https://www.googleapis.com/auth/assistant",
    // Used by components outside this repository.
    "https://www.googleapis.com/auth/youtube.readonly"};
}  // namespace google_auth_provider
