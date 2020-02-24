// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon as zx, runner::component::ComponentNamespaceError, std::convert::From,
    thiserror::Error,
};

/// Error encountered running test component
#[derive(Debug, Error)]
pub enum ComponentError {
    #[error("invalid start info: {:?}", _0)]
    InvalidStartInfo(runner::StartInfoError),

    #[error("error for test {}: {:?}", _0, _1)]
    InvalidArgs(String, anyhow::Error),

    #[error("Cannot run test {}, no namespace was supplied.", _0)]
    MissingNamespace(String),

    #[error("Cannot run test {}, as no outgoing directory was supplied.", _0)]
    MissingOutDir(String),

    #[error("Cannot run suite server: {:?}", _0)]
    ServeSuite(anyhow::Error),

    #[error("{}: {:?}", _0, _1)]
    Fidl(String, fidl::Error),

    #[error("cannot create job: {:?}", _0)]
    CreateJob(zx::Status),

    #[error("cannot duplicate job: {:?}", _0)]
    DuplicateJob(zx::Status),

    #[error("invalid url")]
    InvalidUrl,
}

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

    #[error("{:?}", _0)]
    Io(IoError),

    #[error("can't get test list")]
    ListTest,

    #[error("can't get test list: {:?}", _0)]
    JsonParse(serde_json::error::Error),

    #[error("can't convert to string, refer fxb/4610: {:?}", _0)]
    Utf8ToString(std::str::Utf8Error),

    #[error("{:?}", _0)]
    Log(LogError),
}

/// Error encountered while working with fuchsia::io
#[derive(Debug, Error)]
pub enum IoError {
    #[error("cannot clone proxy: {:?}", _0)]
    CloneProxy(anyhow::Error),

    #[error("can't read file: {:?}", _0)]
    File(anyhow::Error),
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
    Io(IoError),

    #[error("{:?}", _0)]
    Log(LogError),

    #[error("can't convert to string, refer fxb/4610: {:?}", _0)]
    Utf8ToString(std::str::Utf8Error),

    #[error("cannot send start event: {:?}", _0)]
    SendStart(fidl::Error),

    #[error("cannot send finish event: {:?}", _0)]
    SendFinish(fidl::Error),

    #[error("can't get test result: {:?}", _0)]
    JsonParse(serde_json::error::Error),

    #[error("Name in invocation cannot be null")]
    TestCaseName,
}

/// Error while reading/writing log using socket
#[derive(Debug, Error)]
pub enum LogError {
    #[error("can't get logs: {:?}", _0)]
    Read(std::io::Error),

    #[error("can't write logs: {:?}", _0)]
    Write(std::io::Error),
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

impl From<IoError> for EnumerationError {
    fn from(error: IoError) -> Self {
        EnumerationError::Io(error)
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

impl From<IoError> for RunTestError {
    fn from(error: IoError) -> Self {
        RunTestError::Io(error)
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
