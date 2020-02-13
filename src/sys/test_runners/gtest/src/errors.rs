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

    #[error("test run failed: {:?}", _0)]
    RunTest(RunTestError),

    #[error("stream failed: {:?}", _0)]
    Stream(fidl::Error),

    #[error("Cannot send fidl response: {:?}", _0)]
    Response(fidl::Error),
}

/// Error encountered while enumerating test.
#[derive(Debug, Error)]
pub enum EnumerationError {
    #[error("endpoint creation failed: {:?}", _0)]
    CreateEndpoints(fidl::Error),

    #[error("job creation failed: {:?}", _0)]
    CreateJob(zx::Status),

    #[error("can't clone namespace: {:?}", _0)]
    CloneNamespace(ComponentNamespaceError),

    #[error("error launching test: {:?}", _0)]
    LaunchTest(test_runners_lib::LaunchError),

    #[error("cannot clone proxy: {:?}", _0)]
    CloneProxy(fidl::Error),

    #[error("error waiting for test process to exit: {:?}", _0)]
    ProcessExit(zx::Status),

    #[error("error getting info from process: {:?}", _0)]
    ProcessInfo(zx::Status),

    #[error("can't get test list")]
    ListTest,

    #[error("can't open file: {:?}", _0)]
    OpenFile(anyhow::Error),

    #[error("can't read file: {:?}", _0)]
    ReadFile(anyhow::Error),

    #[error("can't get test list: {:?}", _0)]
    JsonParse(serde_json::error::Error),

    #[error("can't convert to string: {:?}", _0)]
    Utf8ToString(std::str::Utf8Error),

    #[error("can't get logs: {:?}", _0)]
    LogError(std::io::Error),
}

/// Error encountered while running test.
#[derive(Debug, Error)]
pub enum RunTestError {}

impl From<EnumerationError> for SuiteServerError {
    fn from(error: EnumerationError) -> Self {
        SuiteServerError::Enumeration(error)
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

impl From<RunTestError> for SuiteServerError {
    fn from(error: RunTestError) -> Self {
        SuiteServerError::RunTest(error)
    }
}
