// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use fidl_fuchsia_ledger_cloud as cloud;
use fuchsia_syslog::fx_log_err;
use std::fmt;
use std::ops::Deref;

pub use cloud::Status;

/// Represents a client error, ie. an error originating from a misuse
/// of the API by the client. Client errors have a status code that
/// will be returned to the client.
#[derive(Debug)]
pub struct ClientError {
    status: cloud::Status,
    underlying_error: Option<Box<dyn Fail>>,
    explanation: Option<String>,
}

impl ClientError {
    /// Adds an explanation to the error.
    pub fn with_explanation<S: Into<String>>(self, explanation: S) -> Self {
        Self { explanation: Some(explanation.into()), ..self }
    }

    /// Returns the status to be sent to Ledger.
    pub fn status(&self) -> Status {
        self.status
    }

    /// Logs this error and returns the underlying status.
    pub fn report(&self) -> Status {
        fx_log_err!("{}", self);
        self.status()
    }
}

impl Fail for ClientError {
    fn name(&self) -> Option<&str> {
        Some("error::ClientError")
    }

    fn cause(&self) -> Option<&dyn Fail> {
        self.underlying_error.as_ref().map(Deref::deref)
    }
}

impl fmt::Display for ClientError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Client error ({:?})", self.status)?;
        if let Some(explanation) = &self.explanation {
            write!(f, ": {}", explanation)?
        }
        if let Some(underlying_error) = &self.underlying_error {
            write!(f, "\n{}", underlying_error)?;
        }
        Ok(())
    }
}

/// Creates a new `ClientError` with the given status.
pub fn client_error(status: Status) -> ClientError {
    ClientError { status, underlying_error: None, explanation: None }
}

pub trait ClientErrorExt {
    /// Wraps an error into a `ClientError` with the given status.
    fn client_error(self, status: Status) -> ClientError;
}

impl<F: Fail> ClientErrorExt for F {
    fn client_error(self, status: Status) -> ClientError {
        ClientError { status, underlying_error: Some(Box::new(self)), explanation: None }
    }
}

/// Extension trait for `Result<(), ClientError>`.
pub trait ResultExt {
    /// Logs the error if there is one and returns the corresponding
    /// status. Otherwise, returns `Status::Ok`.  This is meant to be
    /// used in `responder.send(result.report_if_error())`.
    fn report_if_error(&self) -> Status;
}

impl ResultExt for Result<(), ClientError> {
    fn report_if_error(&self) -> Status {
        match self {
            Ok(()) => Status::Ok,
            Err(client_error) => client_error.report(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn report() {
        let e = client_error(Status::NotFound).with_explanation("test error");
        assert_eq!(e.report(), Status::NotFound);

        #[derive(Debug)]
        struct TestError();
        impl fmt::Display for TestError {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "TestError")
            }
        }
        impl Fail for TestError {}
        let e =
            TestError().client_error(Status::NotSupported).with_explanation("not supported error");
        assert_eq!(e.report(), Status::NotSupported);
    }

    #[test]
    pub fn report_if_error() {
        assert_eq!(Err(client_error(Status::NotFound)).report_if_error(), Status::NotFound);
        assert_eq!(Ok(()).report_if_error(), Status::Ok);
    }
}
