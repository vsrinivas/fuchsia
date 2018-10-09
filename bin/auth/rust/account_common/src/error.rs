// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, Fail};
use fidl_fuchsia_auth_account::Status;

/// An Error type for problems encountered in the account manager and account handler. Each error
/// contains the fuchsia.auth.account.Status that should be reported back to the client and an
/// indication of whether it is fatal.
#[derive(Debug, Fail)]
#[fail(
    display = "AccounManager error, returning {:?}. ({:?})",
    status,
    cause
)]
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
        AccountManagerError {
            status,
            fatal: false,
            cause: None,
        }
    }

    /// Sets a cause on the current error.
    #[allow(dead_code)] // TODO(jsankey): Use this method in the next CL.
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

#[cfg(test)]
mod tests {
    use super::*;
    use failure::format_err;

    const TEST_STATUS: Status = Status::UnknownError;

    #[test]
    fn test_new() {
        let cause = format_err!("Example cause");
        let error = AccountManagerError::new(TEST_STATUS).with_cause(cause);
        assert_eq!(error.status, TEST_STATUS);
        assert!(!error.fatal);
        assert!(error.cause.is_some());
    }

    #[test]
    fn test_from_status() {
        let error: AccountManagerError = TEST_STATUS.into();
        assert_eq!(error.status, TEST_STATUS);
        assert!(!error.fatal);
        assert!(error.cause.is_none());
    }
}
