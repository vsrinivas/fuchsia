// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    runner::component::ComponentNamespaceError, std::convert::From, test_runners_lib::LogError,
    thiserror::Error,
};

/// Error encountered while running suite server
#[derive(Debug, Error)]
pub enum SuiteServerError {
    #[error("test enumeration failed: {:?}", _0)]
    Enumeration(EnumerationError),

    #[error("error running test: {:?}", _0)]
    RunTest(RunTestError),

    #[error("stream failed: {:?}", _0)]
    Stream(fidl::Error),

    #[error("Cannot send fidl response: {:?}", _0)]
    Response(fidl::Error),
}

/// Error encountered while enumerating test.
#[derive(Debug, Error)]
pub enum EnumerationError {
    #[error("{:?}", _0)]
    Namespace(NamespaceError),

    #[error("error launching test: {:?}", _0)]
    LaunchTest(test_runners_lib::LaunchError),

    #[error("can't get test list")]
    ListTest,

    #[error("can't convert to string, refer fxb/4610: {:?}", _0)]
    Utf8ToString(std::str::Utf8Error),

    #[error("{:?}", _0)]
    Log(LogError),
}

/// Error encountered while working runner::component::ComponentNamespace.
#[derive(Debug, Error)]
pub enum NamespaceError {
    #[error("can't clone namespace: {:?}", _0)]
    Clone(ComponentNamespaceError),
}

/// Error encountered while running test.
#[derive(Debug, Error)]
pub enum RunTestError {
    #[error("{:?}", _0)]
    Namespace(NamespaceError),

    #[error("error launching test: {:?}", _0)]
    LaunchTest(test_runners_lib::LaunchError),

    #[error("{:?}", _0)]
    Log(LogError),

    #[error("can't convert to string, refer fxb/4610: {:?}", _0)]
    Utf8ToString(std::str::Utf8Error),

    #[error("cannot send on_finished event: {:?}", _0)]
    SendFinishAllTests(fidl::Error),
}

impl From<EnumerationError> for SuiteServerError {
    fn from(error: EnumerationError) -> Self {
        SuiteServerError::Enumeration(error)
    }
}

impl From<RunTestError> for SuiteServerError {
    fn from(error: RunTestError) -> Self {
        SuiteServerError::RunTest(error)
    }
}

impl From<LogError> for RunTestError {
    fn from(error: LogError) -> Self {
        RunTestError::Log(error)
    }
}

impl From<LogError> for EnumerationError {
    fn from(error: LogError) -> Self {
        EnumerationError::Log(error)
    }
}

impl From<NamespaceError> for EnumerationError {
    fn from(error: NamespaceError) -> Self {
        EnumerationError::Namespace(error)
    }
}

impl From<test_runners_lib::LaunchError> for EnumerationError {
    fn from(error: test_runners_lib::LaunchError) -> Self {
        EnumerationError::LaunchTest(error)
    }
}

impl From<std::str::Utf8Error> for EnumerationError {
    fn from(error: std::str::Utf8Error) -> Self {
        EnumerationError::Utf8ToString(error)
    }
}

impl From<NamespaceError> for RunTestError {
    fn from(error: NamespaceError) -> Self {
        RunTestError::Namespace(error)
    }
}

impl From<test_runners_lib::LaunchError> for RunTestError {
    fn from(error: test_runners_lib::LaunchError) -> Self {
        RunTestError::LaunchTest(error)
    }
}

impl From<std::str::Utf8Error> for RunTestError {
    fn from(error: std::str::Utf8Error) -> Self {
        RunTestError::Utf8ToString(error)
    }
}
