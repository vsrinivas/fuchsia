// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_auth::AuthProviderStatus;
use fidl_fuchsia_identity_external::Error as ApiError;
use fidl_fuchsia_web::NavigationControllerError;
use thiserror::Error;

/// An extension trait to simplify conversion of results based on general errors to
/// AuthProviderErrors.  Usage of this should be removed once the `AuthProvider`
/// protocol is removed.
pub trait AuthProviderResultExt<T, E> {
    /// Wraps the error in an `AuthProviderError` with the supplied `AuthProviderStatus`.
    fn auth_provider_status(self, status: AuthProviderStatus) -> Result<T, AuthProviderError>;
}

impl<T, E> AuthProviderResultExt<T, E> for Result<T, E>
where
    E: Into<Error> + Send + Sync + Sized,
{
    fn auth_provider_status(self, status: AuthProviderStatus) -> Result<T, AuthProviderError> {
        self.map_err(|err| AuthProviderError::new(status).with_cause(err))
    }
}

/// An Error type for problems encountered in the `AuthProvider` protocol. Each
/// error contains the `fuchsia.auth.AuthProviderStatus` that should be
/// reported back to the client. Usage of this should be removed once the
/// `AuthProvider` protocol is removed.
#[derive(Debug, Error)]
#[error("AuthProvider error, returning {:?}. ({:?})", status, cause)]
pub struct AuthProviderError {
    /// The most appropriate `fuchsia.auth.AuthProviderStatus` to describe this problem.
    pub status: AuthProviderStatus,
    /// The cause of this error, if available.
    pub cause: Option<Error>,
}

impl AuthProviderError {
    /// Constructs a new error based on the supplied `AuthProviderStatus`.
    pub fn new(status: AuthProviderStatus) -> Self {
        AuthProviderError { status, cause: None }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
    }
}

/// An extension trait to simplify conversion of results based on general errors to
/// TokenProviderErrors.
pub trait ResultExt<T, E> {
    /// Wraps the error in an `TokenProviderError` with the supplied `ApiError`.
    fn token_provider_error(self, api_error: ApiError) -> Result<T, TokenProviderError>;
}

impl<T, E> ResultExt<T, E> for Result<T, E>
where
    E: Into<Error> + Send + Sync + Sized,
{
    fn token_provider_error(self, api_error: ApiError) -> Result<T, TokenProviderError> {
        self.map_err(|err| TokenProviderError::new(api_error).with_cause(err))
    }
}

/// An Error type for problems encountered in the token provider. Each error
/// contains the ApiError that should be reported back to the client.
#[derive(Debug, Error)]
#[error("TokenProvider error, returning {:?}. ({:?})", api_error, cause)]
pub struct TokenProviderError {
    /// The most appropriate `fuchsia.identity.external.Error` to describe the problem.
    pub api_error: ApiError,
    /// The cause of the error, if available.
    pub cause: Option<Error>,
}

impl TokenProviderError {
    /// Constructs a new error based on the supplied `api_error`.
    pub fn new(api_error: ApiError) -> Self {
        TokenProviderError { api_error, cause: None }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
    }
}

impl From<NavigationControllerError> for TokenProviderError {
    fn from(navigation_error: NavigationControllerError) -> Self {
        TokenProviderError {
            api_error: match navigation_error {
                NavigationControllerError::InvalidUrl
                | NavigationControllerError::InvalidHeader => ApiError::Internal,
            },
            cause: Some(format_err!("Web browser navigation error: {:?}", navigation_error)),
        }
    }
}

impl From<TokenProviderError> for AuthProviderError {
    fn from(token_provider_error: TokenProviderError) -> Self {
        let status = match token_provider_error.api_error {
            ApiError::Unknown => AuthProviderStatus::UnknownError,
            ApiError::Internal => AuthProviderStatus::InternalError,
            ApiError::Config => AuthProviderStatus::InternalError,
            ApiError::UnsupportedOperation => AuthProviderStatus::UnknownError,
            ApiError::InvalidRequest => AuthProviderStatus::BadRequest,
            ApiError::Resource => AuthProviderStatus::UnknownError,
            ApiError::Network => AuthProviderStatus::NetworkError,
            ApiError::Server => AuthProviderStatus::OauthServerError,
            ApiError::InvalidToken => AuthProviderStatus::ReauthRequired,
            ApiError::InsufficientToken => AuthProviderStatus::ReauthRequired,
            ApiError::Aborted => AuthProviderStatus::UserCancelled,
        };
        AuthProviderError { status, cause: token_provider_error.cause }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;

    #[test]
    fn test_create_auth_provider_error() {
        let error = AuthProviderError::new(AuthProviderStatus::UnknownError);
        assert_eq!(error.status, AuthProviderStatus::UnknownError);
        assert!(error.cause.is_none());
    }

    #[test]
    fn test_auth_provider_error_with_cause() {
        let error = AuthProviderError::new(AuthProviderStatus::UnknownError)
            .with_cause(format_err!("cause"));
        assert_eq!(error.status, AuthProviderStatus::UnknownError);
        assert_eq!(format!("{:?}", error.cause.unwrap()), format!("{:?}", format_err!("cause")));
    }

    #[test]
    fn test_auth_provider_result_ext() {
        let result: Result<(), Error> = Err(format_err!("cause"));
        let auth_provider_error =
            result.auth_provider_status(AuthProviderStatus::InternalError).unwrap_err();
        assert_eq!(auth_provider_error.status, AuthProviderStatus::InternalError);
        assert_eq!(
            format!("{:?}", auth_provider_error.cause.unwrap()),
            format!("{:?}", format_err!("cause"))
        );
    }

    #[test]
    fn test_create_error() {
        let error = TokenProviderError::new(ApiError::Unknown);
        assert_eq!(error.api_error, ApiError::Unknown);
        assert!(error.cause.is_none());
    }

    #[test]
    fn test_with_cause() {
        let error = TokenProviderError::new(ApiError::Unknown).with_cause(format_err!("cause"));
        assert_eq!(error.api_error, ApiError::Unknown);
        assert_eq!(format!("{:?}", error.cause.unwrap()), format!("{:?}", format_err!("cause")));
    }

    #[test]
    fn test_result_ext() {
        let result: Result<(), Error> = Err(format_err!("cause"));
        let token_provider_error = result.token_provider_error(ApiError::Internal).unwrap_err();
        assert_eq!(token_provider_error.api_error, ApiError::Internal);
        assert_eq!(
            format!("{:?}", token_provider_error.cause.unwrap()),
            format!("{:?}", format_err!("cause"))
        );
    }

    #[test]
    fn test_from_navigation_controller_error() {
        let token_provider_error = TokenProviderError::from(NavigationControllerError::InvalidUrl);
        assert_eq!(token_provider_error.api_error, ApiError::Internal);
        assert!(token_provider_error.cause.is_some());
    }

    #[test]
    fn test_auth_provider_error_from_token_provider_error() {
        let token_provider_error =
            TokenProviderError::new(ApiError::InsufficientToken).with_cause(format_err!("cause"));
        let auth_provider_error = AuthProviderError::from(token_provider_error);
        assert_eq!(auth_provider_error.status, AuthProviderStatus::ReauthRequired);
        assert_eq!(
            format!("{:?}", auth_provider_error.cause.unwrap()),
            format!("{:?}", format_err!("cause"))
        )
    }
}
