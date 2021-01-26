// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow, fidl_fuchsia_test_manager::LaunchError, thiserror::Error,
    topology_builder::error::Error as TopologyBuilderError,
};

/// Error encountered running test manager
#[derive(Debug, Error)]
pub enum TestManagerError {
    #[error("Error sending response")]
    Response(#[source] fidl::Error),

    #[error("Error serving test manager protocol")]
    Stream(#[source] fidl::Error),

    #[error("Cannot convert to request stream")]
    IntoStream(#[source] fidl::Error),
}

#[derive(Debug, Error)]
pub enum LaunchTestError {
    #[error("Failed to create proxy for archive accessor")]
    CreateProxyForArchiveAccessor(#[source] fidl::Error),

    #[error("Failed to initialize test topology")]
    InitializeTestTopology(#[source] TopologyBuilderError),

    #[error("Failed to create test topology")]
    CreateTestTopology(#[source] TopologyBuilderError),

    #[error("Failed to connect to embedded ArchiveAccessor")]
    ConnectToArchiveAccessor(#[source] anyhow::Error),

    #[error("Failed to connect to TestSuite")]
    ConnectToTestSuite(#[source] anyhow::Error),
}

impl Into<LaunchError> for LaunchTestError {
    fn into(self) -> LaunchError {
        match self {
            Self::CreateProxyForArchiveAccessor(_)
            | Self::InitializeTestTopology(_)
            | Self::ConnectToArchiveAccessor(_) => LaunchError::InternalError,
            Self::CreateTestTopology(_) => LaunchError::InstanceCannotResolve,
            Self::ConnectToTestSuite(_) => LaunchError::FailedToConnectToTestSuite,
        }
    }
}
