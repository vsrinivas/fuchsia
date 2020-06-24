// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type definitions for common errors related to running component or tests.

use {
    crate::{launch::LaunchError, logs::LogError},
    fuchsia_zircon as zx,
    runner::component::ComponentNamespaceError,
    serde_json,
    std::convert::From,
    thiserror::Error,
};

/// Error encountered while enumerating test.
#[derive(Debug, Error)]
pub enum EnumerationError {
    #[error("{:?}", _0)]
    Namespace(NamespaceError),

    #[error("error launching test: {:?}", _0)]
    LaunchTest(LaunchError),

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
    LaunchTest(LaunchError),

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

    #[error("cannot send on_finished event: {:?}", _0)]
    SendFinishAllTests(fidl::Error),

    #[error("Received unexpected exit code {} from test process.", _0)]
    UnexpectedReturnCode(i64),

    #[error("can't get test result: {:?}", _0)]
    JsonParse(serde_json::error::Error),

    #[error("Cannot get test process info: {}", _0)]
    ProcessInfo(zx::Status),

    #[error("Name in invocation cannot be null")]
    TestCaseName,
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

impl From<LaunchError> for EnumerationError {
    fn from(error: LaunchError) -> Self {
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

impl From<LaunchError> for RunTestError {
    fn from(error: LaunchError) -> Self {
        RunTestError::LaunchTest(error)
    }
}

impl From<std::str::Utf8Error> for RunTestError {
    fn from(error: std::str::Utf8Error) -> Self {
        RunTestError::Utf8ToString(error)
    }
}
