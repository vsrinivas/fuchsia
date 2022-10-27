// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_auth::Status::{self as TokenManagerStatus, *};
use fidl_fuchsia_identity_account::Error as AccountApiError;
use fidl_fuchsia_identity_authentication::Error as AuthenticationApiError;
use fidl_fuchsia_identity_authentication::ErrorUnknown as AuthenticationApiErrorUnknown;
use thiserror::Error;

/// An extension trait to simplify conversion of results based on general errors to
/// AccountManagerErrors.
pub trait ResultExt<T, E> {
    /// Wraps the error in a non-fatal `AccountManagerError` with the supplied `AccountApiError`.
    fn account_manager_error(self, api_error: AccountApiError) -> Result<T, AccountManagerError>;
}

impl<T, E> ResultExt<T, E> for Result<T, E>
where
    E: Into<Error> + Send + Sync + Sized,
{
    fn account_manager_error(self, api_error: AccountApiError) -> Result<T, AccountManagerError> {
        self.map_err(|err| AccountManagerError::new(api_error).with_cause(err))
    }
}

/// An Error type for problems encountered in the account manager and account handler. Each error
/// contains the fuchsia.identity.account.Error that should be reported back to the client and an
/// indication of whether it is fatal.
#[derive(Debug, Error)]
#[error("AccountManager error, returning {:?}. ({:?})", api_error, cause)]
pub struct AccountManagerError {
    /// The most appropriate `fuchsia.identity.account.Error` to describe this problem.
    pub api_error: AccountApiError,
    /// Whether this error should be considered fatal, i.e. whether it should
    /// terminate processing of all requests on the current channel.
    pub fatal: bool,
    /// The root cause of this error, if available.
    pub cause: Option<Error>,
}

impl AccountManagerError {
    /// Constructs a new non-fatal error based on the supplied `fuchsia.identity.account.Error`.
    pub fn new(api_error: AccountApiError) -> Self {
        AccountManagerError { api_error, fatal: false, cause: None }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
    }
}

impl From<fidl::Error> for AccountManagerError {
    fn from(error: fidl::Error) -> Self {
        AccountManagerError::new(AccountApiError::Resource).with_cause(error)
    }
}

impl From<AccountApiError> for AccountManagerError {
    fn from(api_error: AccountApiError) -> Self {
        AccountManagerError::new(api_error)
    }
}

impl From<TokenManagerStatus> for AccountManagerError {
    fn from(token_manager_status: TokenManagerStatus) -> Self {
        AccountManagerError {
            api_error: match token_manager_status {
                Ok => AccountApiError::Internal, // Invalid conversion
                InternalError => AccountApiError::Internal,
                InvalidAuthContext => AccountApiError::InvalidRequest,
                InvalidRequest => AccountApiError::InvalidRequest,
                IoError => AccountApiError::Resource,
                NetworkError => AccountApiError::Network,

                AuthProviderServiceUnavailable
                | AuthProviderServerError
                | UserNotFound
                | ReauthRequired
                | UserCancelled
                | UnknownError => AccountApiError::Unknown,
            },
            fatal: false,
            cause: Some(format_err!("Token manager error: {:?}", token_manager_status)),
        }
    }
}

#[allow(clippy::wildcard_in_or_patterns)]
impl From<AuthenticationApiError> for AccountManagerError {
    fn from(authentication_error: AuthenticationApiError) -> Self {
        AccountManagerError {
            api_error: match authentication_error {
                AuthenticationApiError::Internal => AccountApiError::Internal,
                AuthenticationApiError::InvalidAuthContext => AccountApiError::InvalidRequest,
                AuthenticationApiError::InvalidRequest => AccountApiError::InvalidRequest,
                AuthenticationApiError::Resource => AccountApiError::Resource,
                AuthenticationApiError::UnsupportedOperation => {
                    AccountApiError::UnsupportedOperation
                }
                AuthenticationApiError::Unknown
                | AuthenticationApiError::Aborted
                | AuthenticationApiErrorUnknown!() => AccountApiError::Unknown, // TODO(dnordstrom): Needs a non-generic error
            },
            fatal: false,
            cause: Some(format_err!("Authenticator error: {:?}", authentication_error)),
        }
    }
}

impl Into<AccountApiError> for AccountManagerError {
    fn into(self) -> AccountApiError {
        self.api_error
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;

    const TEST_API_ERROR: AccountApiError = AccountApiError::Unknown;

    fn create_test_error() -> Error {
        format_err!("Test error")
    }

    #[test]
    fn test_new() {
        let cause = format_err!("Example cause");
        let cause_str = format!("{:?}", cause);
        let error = AccountManagerError::new(TEST_API_ERROR).with_cause(cause);
        assert_eq!(error.api_error, TEST_API_ERROR);
        assert!(!error.fatal);
        assert_eq!(format!("{:?}", error.cause.unwrap()), cause_str);
    }

    #[test]
    fn test_from_fidl_error() {
        let error: AccountManagerError = fidl::Error::UnexpectedSyncResponse.into();
        assert_eq!(error.api_error, AccountApiError::Resource);
        assert!(!error.fatal);
        assert_eq!(
            format!("{:?}", error.cause.unwrap().downcast::<fidl::Error>().unwrap()),
            format!("{:?}", fidl::Error::UnexpectedSyncResponse),
        );
    }

    #[test]
    fn test_from_identity_error() {
        let error: AccountManagerError = TEST_API_ERROR.into();
        assert_eq!(error.api_error, TEST_API_ERROR);
        assert!(!error.fatal);
        assert!(error.cause.is_none());
    }

    #[test]
    fn test_from_authentication_error() {
        let error: AccountManagerError = AuthenticationApiError::InvalidAuthContext.into();
        let expected_cause = format_err!("Authenticator error: InvalidAuthContext");
        assert_eq!(error.api_error, AccountApiError::InvalidRequest);
        assert!(!error.fatal);
        assert_eq!(format!("{:?}", error.cause.unwrap()), format!("{:?}", expected_cause));
    }

    #[test]
    fn test_to_identity_error() {
        let manager_error = AccountManagerError::new(TEST_API_ERROR);
        let error: AccountApiError = manager_error.into();
        assert_eq!(error, TEST_API_ERROR);
    }

    #[test]
    fn test_account_manager_error() {
        let test_result: Result<(), Error> = Err(create_test_error());
        let wrapped_result = test_result.account_manager_error(TEST_API_ERROR);
        assert_eq!(wrapped_result.as_ref().unwrap_err().api_error, TEST_API_ERROR);
        assert_eq!(
            format!("{:?}", wrapped_result.unwrap_err().cause.unwrap()),
            format!("{:?}", create_test_error())
        );
    }
}
