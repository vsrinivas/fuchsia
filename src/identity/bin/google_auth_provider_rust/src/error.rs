// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, Fail};
use fidl_fuchsia_auth::AuthProviderStatus;

/// An extension trait to simplify conversion of results based on general errors to
/// AuthProviderErrors.
pub trait ResultExt<T, E> {
    /// Wraps the error in an `AuthProviderError` with the supplied `AuthProviderStatus`.
    fn auth_provider_status(self, status: AuthProviderStatus) -> Result<T, AuthProviderError>;
}

impl<T, E> ResultExt<T, E> for Result<T, E>
where
    E: Into<Error> + Send + Sync + Sized,
{
    fn auth_provider_status(self, status: AuthProviderStatus) -> Result<T, AuthProviderError> {
        self.map_err(|err| AuthProviderError::new(status).with_cause(err))
    }
}

/// An Error type for problems encountered in the auth provider. Each error contains the
/// `fuchsia.auth.AuthProviderStatus` that should be reported back to the client.
/// TODO(satsukiu): This is general across auth providers, once there are multiple
/// providers this should be moved out to a general crate.
#[derive(Debug, Fail)]
#[fail(display = "AuthProvider error, returning {:?}. ({:?})", status, cause)]
pub struct AuthProviderError {
    /// The most appropriate `fuchsia.auth.AuthProviderStatus` to describe this problem.
    pub status: AuthProviderStatus,
    /// The cause of this error, if available.
    pub cause: Option<Error>,
}

impl AuthProviderError {
    /// Constructs a new non-fatal error based on the supplied `AuthProviderStatus`.
    pub fn new(status: AuthProviderStatus) -> Self {
        AuthProviderError { status, cause: None }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::format_err;

    #[test]
    fn test_create_error() {
        let error = AuthProviderError::new(AuthProviderStatus::UnknownError);
        assert_eq!(error.status, AuthProviderStatus::UnknownError);
        assert!(error.cause.is_none());
    }

    #[test]
    fn test_with_cause() {
        let error = AuthProviderError::new(AuthProviderStatus::UnknownError)
            .with_cause(format_err!("cause"));
        assert_eq!(error.status, AuthProviderStatus::UnknownError);
        assert_eq!(format!("{:?}", error.cause.unwrap()), format!("{:?}", format_err!("cause")));
    }

    #[test]
    fn test_result_ext() {
        let result: Result<(), Error> = Err(format_err!("cause"));
        let auth_provider_error =
            result.auth_provider_status(AuthProviderStatus::InternalError).unwrap_err();
        assert_eq!(auth_provider_error.status, AuthProviderStatus::InternalError);
        assert_eq!(
            format!("{:?}", auth_provider_error.cause.unwrap()),
            format!("{:?}", format_err!("cause"))
        );
    }
}
