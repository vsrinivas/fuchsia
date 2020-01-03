// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_identity_external::Error as ApiError;
use fidl_fuchsia_web::NavigationControllerError;
use thiserror::Error;

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

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;

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
}
