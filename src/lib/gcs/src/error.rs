// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A flexible set of errors which give actionable results.

use thiserror::Error;

/// Errors specific to this lib, some may be addressed by the caller.
#[derive(Error, Debug)]
pub enum GcsError {
    /// The refresh token is no longer valid (e.g. expired or revoked).
    /// Do something similar to `gsutil config` or new_refresh_token() to
    /// generate a new refresh token and try again.
    #[error("GCS refresh token is invalid. Please generate a new GCS OAUTH2 refresh token.")]
    NeedNewRefreshToken,

    /// The access token is no longer valid (e.g. expired or revoked). Access
    /// tokens are short lived and don't require user interaction to renew.
    /// Refresh the access token and try the action again.
    #[error("GCS access token is invalid. This should be handled. Report as a bug.")]
    NeedNewAccessToken,

    /// GCS returned an empty response for the request.
    #[error("The requested GCS bucket + path contains no data (not found): {0}, {1}.")]
    NotFound(String, String),

    /// The authorization code is empty string (or None). Likely a mistake in
    /// the calling application (e.g. calling new_with_code() with an empty
    /// string).
    #[error("Empty authorization code passed to GCS. Report as a bug.")]
    MissingAuthCode,

    /// Likely a mistake in the calling application.
    #[error("A GCS refresh token is required to gain a new access token. Report as a bug.")]
    MissingRefreshToken,

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error(
        "Unable to refresh GCS access token: HTTP {0}. Check network connection and try again."
    )]
    RefreshAccessError(http::StatusCode),

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error("GCS HttpResponseError: {0}. Check network connection and try again.")]
    HttpResponseError(http::StatusCode),

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error("GCS HttpError: {0}. Check network connection and try again.")]
    HttpError(#[from] http::Error),

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error("GCS HyperError: {0}. Check network connection and try again.")]
    HyperError(#[from] hyper::Error),

    /// Possible in-transit corruption, but more likely the GCS protocol
    /// changed. May need to update GCS lib.
    #[error("GCS SerdeError: {0}. Report as a bug.")]
    SerdeError(#[from] serde_json::Error),
}
