// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_test_manager as ftest_manager,
    std::{fmt, sync::Arc},
    thiserror::Error,
};

#[derive(Debug, Clone)]
pub enum Outcome {
    Passed,
    Failed,
    Inconclusive,
    Timedout,
    /// Suite was stopped prematurely due to cancellation by the user.
    Cancelled,
    /// Suite did not report completion.
    // TODO(fxbug.dev/90037) - this outcome indicates an internal error as test manager isn't
    // sending expected events. We should return an error instead.
    DidNotFinish,
    Error {
        origin: Arc<RunTestSuiteError>,
    },
}

impl Outcome {
    pub(crate) fn error<E: Into<RunTestSuiteError>>(e: E) -> Self {
        Self::Error { origin: Arc::new(e.into()) }
    }
}

impl PartialEq for Outcome {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Passed, Self::Passed)
            | (Self::Failed, Self::Failed)
            | (Self::Inconclusive, Self::Inconclusive)
            | (Self::Timedout, Self::Timedout)
            | (Self::Cancelled, Self::Cancelled)
            | (Self::DidNotFinish, Self::DidNotFinish) => true,
            (Self::Error { origin }, Self::Error { origin: other_origin }) => {
                format!("{}", origin.as_ref()) == format!("{}", other_origin.as_ref())
            }
            (_, _) => false,
        }
    }
}

impl fmt::Display for Outcome {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Outcome::Passed => write!(f, "PASSED"),
            Outcome::Failed => write!(f, "FAILED"),
            Outcome::Inconclusive => write!(f, "INCONCLUSIVE"),
            Outcome::Timedout => write!(f, "TIMED OUT"),
            Outcome::Cancelled => write!(f, "CANCELLED"),
            Outcome::DidNotFinish => write!(f, "DID_NOT_FINISH"),
            Outcome::Error { .. } => write!(f, "ERROR"),
        }
    }
}

#[derive(Error, Debug)]
/// An enum of the different errors that may be encountered while running
/// a test.
pub enum RunTestSuiteError {
    #[error("fidl error: {0:?}")]
    Fidl(#[from] fidl::Error),
    #[error("error launching test suite: {}", convert_launch_error_to_str(.0))]
    Launch(ftest_manager::LaunchError),
    #[error("error reporting test results: {0:?}")]
    Io(#[from] std::io::Error),
    #[error("unexpected event: {0:?}")]
    UnexpectedEvent(#[from] UnexpectedEventError),
}

/// An error returned when test manager reports an unexpected event.
/// This could occur if test manager violates guarantees about event
/// ordering.
#[derive(Error, Debug)]
pub enum UnexpectedEventError {
    #[error(
        "received a 'started' event for case with id {identifier:?} but no 'case_found' event"
    )]
    CaseStartedButNotFound { identifier: u32 },
    #[error(
        "invalid case event to '{next_state:?}' received while in state '{last_state:?}' for case {test_case_name:?} with id {identifier:?}"
    )]
    InvalidCaseEvent {
        last_state: Lifecycle,
        next_state: Lifecycle,
        test_case_name: String,
        identifier: u32,
    },
    #[error(
        "received an 'artifact' event for case with id {identifier:?} but no 'case_found' event"
    )]
    CaseArtifactButNotFound { identifier: u32 },
    #[error(
        "received an 'artifact' event for case with id {identifier:?} but the case is already finished"
    )]
    CaseArtifactButFinished { identifier: u32 },
    #[error(
        "received a '{next_state:?}' event for case with id {identifier:?} but no 'case_found' event"
    )]
    CaseEventButNotFound { next_state: Lifecycle, identifier: u32 },
    #[error("received a 'stopped' event for case with id {identifier:?} but no 'started' event")]
    UnrecognizedCaseStatus { status: ftest_manager::CaseStatus, identifier: u32 },
    #[error("server closed channel without reporting finish for cases: {cases:?}")]
    CasesDidNotFinish { cases: Vec<String> },
    #[error("invalid suite event to '{next_state:?}' received while in state '{last_state:?}'")]
    InvalidSuiteEvent { last_state: Lifecycle, next_state: Lifecycle },
    #[error("received an unhandled suite status: {status:?}")]
    UnrecognizedSuiteStatus { status: ftest_manager::SuiteStatus },
    #[error("server closed channel without reporting a result for the suite")]
    SuiteDidNotReportStop,
    #[error("received an InternalError suite status")]
    InternalErrorSuiteStatus,
    #[error("missing required field {field} in {containing_struct}")]
    MissingRequiredField { containing_struct: &'static str, field: &'static str },
}

/// Lifecycle of a test suite or test case.
/// This is internal implementation for ::crate::run, but is located here so it can be reported
/// for debugging via RunTestSuiteError.
#[derive(Clone, Copy, Debug)]
pub enum Lifecycle {
    Found,
    Started,
    Stopped,
    Finished,
}

impl RunTestSuiteError {
    /// Returns true iff the error variant indicates an internal error in
    /// Test Manager or ffx.
    pub fn is_internal_error(&self) -> bool {
        match self {
            Self::Fidl(_) => true,
            Self::Launch(ftest_manager::LaunchError::InternalError) => true,
            Self::Launch(_) => false,
            Self::Io(_) => true,
            Self::UnexpectedEvent(_) => true,
        }
    }
}

impl From<ftest_manager::LaunchError> for RunTestSuiteError {
    fn from(launch: ftest_manager::LaunchError) -> Self {
        Self::Launch(launch)
    }
}

fn convert_launch_error_to_str(e: &ftest_manager::LaunchError) -> &'static str {
    match e {
        ftest_manager::LaunchError::CaseEnumeration => "Cannot enumerate test. This may mean `fuchsia.test.Suite` was not configured correctly. Refer to: \
        https://fuchsia.dev/go/components/test-errors",
        ftest_manager::LaunchError::ResourceUnavailable => "Resource unavailable",
        ftest_manager::LaunchError::InstanceCannotResolve => "Cannot resolve test.",
        ftest_manager::LaunchError::InvalidArgs => {
            "Invalid args passed to builder while adding suite. Please file bug"
        }
        ftest_manager::LaunchError::FailedToConnectToTestSuite => {
            "Cannot communicate with the tests. This may mean `fuchsia.test.Suite` was not \
            configured correctly. Refer to: \
            https://fuchsia.dev/go/components/test-errors"
        }
        ftest_manager::LaunchError::InternalError => "Internal error, please file bug",
        ftest_manager::LaunchError::NoMatchingCases =>
            // TODO(satsukiu): Ideally, we would expose these error enums up through the library
            // and define the error messages in the main files for each tool. This would allow each
            // tool (ffx/fx/run-test-suite) to give the correct flags.
            "No test cases matched the specified filters.\n\
            If you specified a test filter, verify the available test cases with \
            'ffx test list-cases <test suite url>'.\n\
            If the list of available tests contains only a single test case called either \
            'legacy_test' or 'main', the suite likely uses either the legacy_test_runner or \
            elf_test_runner. In these cases, --test-filter will not work. Instead, \
            you can pass test arguments directly to the test instead. Refer to: \
            https://fuchsia.dev/go/components/test-runners",
        ftest_manager::LaunchError::InvalidManifest => "The test manifest is invalid or has invalid facets/arguments. Please check logs for detailed error.",
        ftest_manager::LaunchErrorUnknown!() => "Unrecognized launch error",
    }
}
