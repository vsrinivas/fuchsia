// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_auth::{AuthProviderStatus, Status};
use fidl_fuchsia_identity_external::Error as ExternalApiError;
use thiserror::Error;
use token_cache::AuthCacheError;
use token_store::AuthDbError;

/// An extension trait to simplify conversion of results based on general errors to
/// TokenManagerErrors.
pub trait ResultExt<T, E> {
    /// Wraps the error in a non-fatal `TokenManagerError` with the supplied `Status`.
    fn token_manager_status(self, status: Status) -> Result<T, TokenManagerError>;
}

impl<T, E> ResultExt<T, E> for Result<T, E>
where
    E: Into<Error> + Send + Sync + Sized,
{
    fn token_manager_status(self, status: Status) -> Result<T, TokenManagerError> {
        self.map_err(|err| TokenManagerError::new(status).with_cause(err))
    }
}

/// An Error type for problems encountered in the token manager. Each error contains the
/// `fuchsia.auth.Status` that should be reported back to the client and an indication of whether
/// it is fatal.
#[derive(Debug, Error)]
#[error("TokenManager error, returning {:?}. ({:?})", status, cause)]
pub struct TokenManagerError {
    /// The most appropriate `fuchsia.auth.Status` to describe this problem.
    pub status: Status,
    /// Whether this error should be considered fatal, i.e. whether it should terminate processing
    /// of all requests on the current channel.
    pub fatal: bool,
    /// The cause of this error, if available.
    pub cause: Option<Error>,
}

impl TokenManagerError {
    /// Constructs a new non-fatal error based on the supplied `Status`.
    pub fn new(status: Status) -> Self {
        TokenManagerError { status, fatal: false, cause: None }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
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
        TokenManagerError { status, fatal, cause: Some(Error::from(auth_db_error)) }
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

impl From<ExternalApiError> for TokenManagerError {
    fn from(external_api_error: ExternalApiError) -> Self {
        TokenManagerError {
            status: match external_api_error {
                ExternalApiError::Unknown => Status::UnknownError,
                ExternalApiError::Internal => Status::InternalError,
                ExternalApiError::Config => Status::AuthProviderServiceUnavailable,
                ExternalApiError::UnsupportedOperation => Status::UnknownError,
                ExternalApiError::InvalidRequest => Status::InvalidRequest,
                ExternalApiError::Resource => Status::IoError,
                ExternalApiError::Network => Status::NetworkError,
                ExternalApiError::Server => Status::AuthProviderServerError,
                ExternalApiError::InvalidToken => Status::ReauthRequired,
                ExternalApiError::InsufficientToken => Status::ReauthRequired,
                ExternalApiError::Aborted => Status::UserCancelled,
            },
            fatal: false,
            cause: Some(format_err!("Auth provider error: {:?}", external_api_error)),
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
            cause: Some(format_err!("Auth provider error: {:?}", auth_provider_status)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;

    const TEST_STATUS: Status = Status::UnknownError;

    fn create_test_error() -> Error {
        format_err!("Test error")
    }

    #[test]
    fn test_new() {
        let cause = format_err!("Example cause");
        let error = TokenManagerError::new(TEST_STATUS).with_cause(cause);
        assert_eq!(error.status, TEST_STATUS);
        assert!(!error.fatal);
        assert!(error.cause.is_some());
    }

    #[test]
    fn test_from_status() {
        let error: TokenManagerError = TEST_STATUS.into();
        assert_eq!(error.status, TEST_STATUS);
        assert!(!error.fatal);
        assert!(error.cause.is_none());
    }

    #[test]
    fn test_token_manager_status() {
        let test_result: Result<(), Error> = Err(create_test_error());
        let wrapped_result = test_result.token_manager_status(TEST_STATUS);
        assert_eq!(wrapped_result.as_ref().unwrap_err().status, TEST_STATUS);
        assert_eq!(
            format!("{:?}", wrapped_result.unwrap_err().cause.unwrap()),
            format!("{:?}", create_test_error())
        );
    }

    #[test]
    fn test_from_auth_db_error() {
        let err = TokenManagerError::from(AuthDbError::CredentialNotFound);
        let err_fatal = TokenManagerError::from(AuthDbError::DbInvalid);
        assert_eq!(
            (format!("{:?}", err.cause.as_ref().unwrap()), err.fatal, err.status),
            ("credential not found".to_string(), false, Status::UserNotFound)
        );
        assert_eq!(
            (format!("{:?}", err_fatal.cause.as_ref().unwrap()), err_fatal.fatal, err_fatal.status),
            ("database contents could not be parsed".to_string(), true, Status::InternalError)
        );
    }

    #[test]
    fn test_from_auth_cache_error() {
        let err = TokenManagerError::from(AuthCacheError::InvalidArguments);
        assert_eq!(
            (format!("{:?}", err.cause.as_ref().unwrap()), err.fatal, err.status),
            ("invalid argument".to_string(), false, Status::InvalidRequest)
        );
    }

    #[test]
    fn test_from_auth_provider_status() {
        let err = TokenManagerError::from(AuthProviderStatus::ReauthRequired);
        assert_eq!((err.fatal, err.status), (false, Status::ReauthRequired));
        assert!(err.cause.is_some());
    }
}
