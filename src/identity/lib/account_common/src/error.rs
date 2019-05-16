// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, Fail};
use fidl_fuchsia_auth::Status::{self as TokenManagerStatus, *};
use fidl_fuchsia_auth_account::Status;

/// An extension trait to simplify conversion of results based on general errors to
/// AccountManagerErrors.
pub trait ResultExt<T, E> {
    /// Wraps the error in a non-fatal `AccountManagerError` with the supplied `Status`.
    fn account_manager_status(self, status: Status) -> Result<T, AccountManagerError>;
}

impl<T, E> ResultExt<T, E> for Result<T, E>
where
    E: Into<Error> + Send + Sync + Sized,
{
    fn account_manager_status(self, status: Status) -> Result<T, AccountManagerError> {
        self.map_err(|err| AccountManagerError::new(status).with_cause(err))
    }
}

/// An Error type for problems encountered in the account manager and account handler. Each error
/// contains the fuchsia.auth.account.Status that should be reported back to the client and an
/// indication of whether it is fatal.
#[derive(Debug, Fail)]
#[fail(display = "AccountManager error, returning {:?}. ({:?})", status, cause)]
pub struct AccountManagerError {
    /// The most appropriate `fuchsia.auth.account.Status` to describe this problem.
    pub status: Status,
    /// Whether this error should be considered fatal, i.e. whether it should
    /// terminate processing of all requests on the current channel.
    pub fatal: bool,
    /// The root cause of this error, if available.
    pub cause: Option<Error>,
}

impl AccountManagerError {
    /// Constructs a new non-fatal error based on the supplied `Status`.
    pub fn new(status: Status) -> Self {
        AccountManagerError { status, fatal: false, cause: None }
    }

    /// Sets a cause on the current error.
    pub fn with_cause<T: Into<Error>>(mut self, cause: T) -> Self {
        self.cause = Some(cause.into());
        self
    }
}

impl From<Status> for AccountManagerError {
    fn from(status: Status) -> Self {
        AccountManagerError::new(status)
    }
}

impl From<TokenManagerStatus> for AccountManagerError {
    fn from(token_manager_status: TokenManagerStatus) -> Self {
        AccountManagerError {
            status: match token_manager_status {
                Ok => Status::Ok, // It is not adviced to create an error with an "ok" status
                InternalError => Status::InternalError,
                InvalidAuthContext => Status::InvalidRequest,
                InvalidRequest => Status::InvalidRequest,
                IoError => Status::IoError,
                NetworkError => Status::NetworkError,

                AuthProviderServiceUnavailable
                | AuthProviderServerError
                | UserNotFound
                | ReauthRequired
                | UserCancelled
                | UnknownError => Status::UnknownError,
            },
            fatal: false,
            cause: Some(format_err!("Token manager error: {:?}", token_manager_status)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::format_err;

    const TEST_STATUS: Status = Status::UnknownError;

    fn create_test_error() -> Error {
        format_err!("Test error")
    }

    #[test]
    fn test_new() {
        let cause = format_err!("Example cause");
        let cause_str = format!("{:?}", cause);
        let error = AccountManagerError::new(TEST_STATUS).with_cause(cause);
        assert_eq!(error.status, TEST_STATUS);
        assert!(!error.fatal);
        assert_eq!(format!("{:?}", error.cause.unwrap()), cause_str);
    }

    #[test]
    fn test_from_status() {
        let error: AccountManagerError = TEST_STATUS.into();
        assert_eq!(error.status, TEST_STATUS);
        assert!(!error.fatal);
        assert!(error.cause.is_none());
    }

    #[test]
    fn test_account_manager_status() {
        let test_result: Result<(), Error> = Err(create_test_error());
        let wrapped_result = test_result.account_manager_status(TEST_STATUS);
        assert_eq!(wrapped_result.as_ref().unwrap_err().status, TEST_STATUS);
        assert_eq!(
            format!("{:?}", wrapped_result.unwrap_err().cause.unwrap()),
            format!("{:?}", create_test_error())
        );
    }
}
