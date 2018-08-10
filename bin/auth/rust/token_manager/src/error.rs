// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use auth_cache::AuthCacheError;
use auth_store::AuthDbError;
use failure::Error;
use fidl_fuchsia_auth::{AuthProviderStatus, Status};
use futures::future::{ready as fready, FutureObj};

/// An Error type for problems encountered in the token manager. Each error
/// contains the fuchsia.auth.Status that should be reported back to the
/// client and an indication of whether it is fatal.

#[derive(Debug, Fail)]
#[fail(
    display = "TokenManager error, returning {:?}. ({:?})",
    status,
    cause
)]
pub struct TokenManagerError {
    /// The most appropriate `fuchsia.auth.Status` to describe this problem.
    pub status: Status,
    /// Whether this error should be considered fatal, i.e. whether it should
    /// terminate processing of all requests on the current channel.
    pub fatal: bool,
    /// The root cause of this error, if available.
    pub cause: Option<Error>,
}

impl TokenManagerError {
    /// Constructs a new non-fatal error based on the supplied `Status`.
    pub fn new(status: Status) -> Self {
        TokenManagerError {
            status,
            fatal: false,
            cause: None,
        }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
    }

    /// Convenience method to create a `Box` of a `FutureResult` based on this
    /// error.
    pub fn to_boxed_future<T: Send + 'static>(
        self,
    ) -> FutureObj<'static, Result<T, TokenManagerError>> {
        FutureObj::new(Box::new(fready(Err(self))))
    }
}

impl From<Status> for TokenManagerError {
    fn from(status: Status) -> Self {
        TokenManagerError::new(status)
    }
}

impl From<AuthDbError> for TokenManagerError {
    fn from(auth_db_error: AuthDbError) -> Self {
        let (status, fatal) = match &auth_db_error {
            AuthDbError::InvalidArguments => (Status::InvalidRequest, true),
            AuthDbError::DbInvalid => (Status::InternalError, true),
            AuthDbError::CredentialNotFound => (Status::UserNotFound, false),
            AuthDbError::SerializationError => (Status::InternalError, false),
            _ => (Status::UnknownError, false),
        };
        TokenManagerError {
            status,
            fatal,
            cause: Some(Error::from(auth_db_error)),
        }
    }
}

impl From<AuthCacheError> for TokenManagerError {
    fn from(auth_cache_error: AuthCacheError) -> Self {
        TokenManagerError {
            status: match auth_cache_error {
                AuthCacheError::InvalidArguments => Status::InvalidRequest,
                AuthCacheError::KeyNotFound => Status::UserNotFound,
            },
            // No cache failures are persistent and hence none are fatal.
            fatal: false,
            cause: Some(Error::from(auth_cache_error)),
        }
    }
}

impl From<AuthProviderStatus> for TokenManagerError {
    fn from(auth_provider_status: AuthProviderStatus) -> Self {
        TokenManagerError {
            status: match auth_provider_status {
                AuthProviderStatus::BadRequest => Status::InvalidRequest,
                AuthProviderStatus::OauthServerError => Status::AuthProviderServerError,
                AuthProviderStatus::UserCancelled => Status::UserCancelled,
                AuthProviderStatus::ReauthRequired => Status::ReauthRequired,
                AuthProviderStatus::NetworkError => Status::NetworkError,
                AuthProviderStatus::InternalError => Status::InternalError,
                _ => Status::UnknownError,
            },
            // Auth provider failures are localized to the particular provider that
            // produced them and therefore none are fatal to an entire TokenManager.
            fatal: false,
            cause: Some(format_err!(
                "Auth provider error: {:?}",
                auth_provider_status
            )),
        }
    }
}
