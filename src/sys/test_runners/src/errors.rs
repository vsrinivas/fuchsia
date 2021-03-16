// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type definitions for common errors related to running component or tests.

use {
    crate::{elf::KernelError, launch::LaunchError, logs::LogError},
    fuchsia_zircon as zx,
    runner::component::ComponentNamespaceError,
    serde_json,
    std::sync::Arc,
    thiserror::Error,
};
/// Error encountered while enumerating test.
///
/// This must be `Clone`-able because enumeration futures are memoized.
#[derive(Debug, Error, Clone)]
pub enum EnumerationError {
    #[error("{:?}", _0)]
    Namespace(#[source] Arc<NamespaceError>),

    #[error("error launching test: {:?}", _0)]
    LaunchTest(#[source] Arc<LaunchError>),

    #[error("{:?}", _0)]
    Io(#[source] Arc<IoError>),

    #[error("can't get test list")]
    ListTest,

    #[error("can't get test list: {:?}", _0)]
    JsonParse(#[source] Arc<serde_json::error::Error>),

    #[error("can't convert to string, refer fxbug.dev/4610: {:?}", _0)]
    Utf8ToString(#[from] std::str::Utf8Error),

    #[error("{:?}", _0)]
    Log(#[from] Arc<LogError>),

    #[error("{:?}", _0)]
    Kernel(#[from] Arc<KernelError>),
}

impl From<NamespaceError> for EnumerationError {
    fn from(e: NamespaceError) -> Self {
        EnumerationError::Namespace(Arc::new(e))
    }
}

impl From<KernelError> for EnumerationError {
    fn from(e: KernelError) -> Self {
        EnumerationError::Kernel(Arc::new(e))
    }
}

impl From<LaunchError> for EnumerationError {
    fn from(e: LaunchError) -> Self {
        EnumerationError::LaunchTest(Arc::new(e))
    }
}

impl From<IoError> for EnumerationError {
    fn from(e: IoError) -> Self {
        EnumerationError::Io(Arc::new(e))
    }
}

impl From<serde_json::error::Error> for EnumerationError {
    fn from(e: serde_json::error::Error) -> Self {
        EnumerationError::JsonParse(Arc::new(e))
    }
}

impl From<LogError> for EnumerationError {
    fn from(e: LogError) -> Self {
        EnumerationError::Log(Arc::new(e))
    }
}

/// Error encountered while working with fuchsia::io
#[derive(Debug, Error)]
pub enum IoError {
    #[error("cannot clone proxy: {:?}", _0)]
    CloneProxy(#[source] anyhow::Error),

    #[error("can't read file: {:?}", _0)]
    File(#[source] anyhow::Error),
}

/// Error encountered while working runner::component::ComponentNamespace.
#[derive(Debug, Error)]
pub enum NamespaceError {
    #[error("can't clone namespace: {:?}", _0)]
    Clone(#[from] ComponentNamespaceError),
}

/// Error returned when validating arguments.
#[derive(Debug, Error)]
pub enum ArgumentError {
    #[error("Restricted argument passed: {}", _0)]
    RestrictedArg(String),
}

/// Error encountered while running test.
#[derive(Debug, Error)]
pub enum RunTestError {
    #[error("{:?}", _0)]
    Namespace(#[from] NamespaceError),

    #[error("error launching test: {:?}", _0)]
    LaunchTest(#[from] LaunchError),

    #[error("{:?}", _0)]
    Io(#[from] IoError),

    #[error("{:?}", _0)]
    Log(#[from] LogError),

    #[error("can't convert to string, refer fxbug.dev/4610: {:?}", _0)]
    Utf8ToString(#[from] std::str::Utf8Error),

    #[error("cannot send start event: {:?}", _0)]
    SendStart(#[source] fidl::Error),

    #[error("cannot send finish event: {:?}", _0)]
    SendFinish(#[source] fidl::Error),

    #[error("cannot send on_finished event: {:?}", _0)]
    SendFinishAllTests(#[source] fidl::Error),

    #[error("Received unexpected exit code {} from test process.", _0)]
    UnexpectedReturnCode(i64),

    #[error("can't get test result: {:?}", _0)]
    JsonParse(#[from] serde_json::error::Error),

    #[error("Cannot get test process info: {}", _0)]
    ProcessInfo(#[source] zx::Status),

    #[error("Name in invocation cannot be null")]
    TestCaseName,

    #[error("error reloading list of tests: {:?}", _0)]
    EnumerationError(#[from] EnumerationError),
}

/// Error encountered while calling fdio operations.
#[derive(Debug, PartialEq, Eq, Error, Clone)]
pub enum FdioError {
    #[error("Cannot create file descriptor: {:?}", _0)]
    Create(zx::Status),

    #[error("Cannot clone file descriptor: {:?}", _0)]
    Clone(zx::Status),

    #[error("Cannot transfer file descriptor: {:?}", _0)]
    Transfer(zx::Status),
}
