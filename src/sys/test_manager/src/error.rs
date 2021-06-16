// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow, fidl_fuchsia_developer_remotecontrol::StreamError,
    fidl_fuchsia_test_manager::LaunchError,
    fuchsia_component_test::error::Error as RealmBuilderError, thiserror::Error,
};

/// Error encountered running test manager
#[derive(Debug, Error)]
pub enum TestManagerError {
    #[error("Error sending response: {0:?}")]
    Response(#[source] fidl::Error),

    #[error("Error serving test manager protocol: {0:?}")]
    Stream(#[source] fidl::Error),

    #[error("Cannot convert to request stream: {0:?}")]
    IntoStream(#[source] fidl::Error),
}

#[derive(Debug, Error)]
pub enum LaunchTestError {
    #[error("Failed to create proxy for archive accessor: {0:?}")]
    CreateProxyForArchiveAccessor(#[source] fidl::Error),

    #[error("Failed to initialize test realm: {0:?}")]
    InitializeTestRealm(#[source] RealmBuilderError),

    #[error("Failed to create test realm: {0:?}")]
    CreateTestRealm(#[source] RealmBuilderError),

    #[error("Failed to connect to embedded ArchiveAccessor: {0:?}")]
    ConnectToArchiveAccessor(#[source] anyhow::Error),

    #[error("Failed to connect to TestSuite: {0:?}")]
    ConnectToTestSuite(#[source] anyhow::Error),

    #[error("Failed to stream logs from embedded Archivist: {0:?}")]
    StreamIsolatedLogs(StreamError),
}

impl Into<LaunchError> for LaunchTestError {
    fn into(self) -> LaunchError {
        match self {
            Self::CreateProxyForArchiveAccessor(_)
            | Self::InitializeTestRealm(_)
            | Self::ConnectToArchiveAccessor(_)
            | Self::StreamIsolatedLogs(_) => LaunchError::InternalError,
            Self::CreateTestRealm(_) => LaunchError::InstanceCannotResolve,
            Self::ConnectToTestSuite(_) => LaunchError::FailedToConnectToTestSuite,
        }
    }
}
