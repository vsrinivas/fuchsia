// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_test_manager as ftest_manager;
use thiserror::Error;

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
        "received duplicate 'started' events for case {test_case_name:?} with id {identifier:?}"
    )]
    CaseStartedTwice { test_case_name: String, identifier: u32 },
    #[error(
        "received an 'artifact' event for case with id {identifier:?} but no 'case_found' event"
    )]
    CaseArtifactButNotFound { identifier: u32 },
    #[error(
        "received a 'stopped' event for case with id {identifier:?} but no 'case_found' event"
    )]
    CaseStoppedButNotFound { identifier: u32 },
    #[error("received a 'stopped' event for case with id {identifier:?} but no 'started' event")]
    CaseStoppedButNotStarted { test_case_name: String, identifier: u32 },
    #[error("received an unhandled case status for case with id {identifier:?}: {status:?}")]
    UnrecognizedCaseStatus { status: ftest_manager::CaseStatus, identifier: u32 },
    #[error("received an unhandled suite status: {status:?}")]
    UnrecognizedSuiteStatus { status: ftest_manager::SuiteStatus },
    #[error("received an InternalError suite status")]
    InternalErrorSuiteStatus,
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
        ftest_manager::LaunchError::CaseEnumeration => "Cannot enumerate test. This may mean `fuchsia.test.Suite` was not \
        configured correctly. Refer to: \
        https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#troubleshoot-test",
        ftest_manager::LaunchError::ResourceUnavailable => "Resource unavailable",
        ftest_manager::LaunchError::InstanceCannotResolve => "Cannot resolve test.",
        ftest_manager::LaunchError::InvalidArgs => {
            "Invalid args passed to builder while adding suite. Please file bug"
        }
        ftest_manager::LaunchError::FailedToConnectToTestSuite => {
            "Cannot communicate with the tests. This may mean `fuchsia.test.Suite` was not \
            configured correctly. Refer to: \
            https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#troubleshoot-test"
        }
        ftest_manager::LaunchError::InternalError => "Internal error, please file bug",
        ftest_manager::LaunchErrorUnknown!() => "Unrecognized launch error",
    }
}
