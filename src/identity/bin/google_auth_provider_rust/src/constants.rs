// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The constants module contains default values used for token exchange.
// The values in this file should be kept in sync with the values in
// src/identity/bin/google_auth_provider/constants.h

use lazy_static::lazy_static;
use url::Url;

/// Client ID used to identify Fuchsia.
pub const FUCHSIA_CLIENT_ID: &str =
    "934259141868-rejmm4ollj1bs7th1vg2ur6antpbug79.apps.googleusercontent.com";

lazy_static! {
    /// Default scopes requested for Oauth tokens.
    pub static ref DEFAULT_SCOPES: String = vec![
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
        "https://www.googleapis.com/auth/youtube.readonly"
    ].join(" ");
    /// URL to redirect to during authorization flow.
    pub static ref REDIRECT_URI: Url =
        Url::parse("https://localhost/fuchsiaoauth2redirect").unwrap();
    /// Entry point URL for authentication with Google.
    pub static ref AUTHORIZE_URI: Url =
        Url::parse("https://accounts.google.com/o/oauth2/v2/auth").unwrap();
    /// URL for OAuth token exchange requests.
    pub static ref OAUTH_URI: Url =
        Url::parse("https://www.googleapis.com/oauth2/v4/token").unwrap();
}
