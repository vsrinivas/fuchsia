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

    #[error("Failed to resolve test: {0:?}")]
    ResolveTest(#[source] anyhow::Error),

    #[error("Failed to read manifest: {0}")]
    ManifestIo(mem_util::DataError),

    #[error("Resolver returned invalid manifest data")]
    InvalidResolverData,

    #[error("Invalid manifest: {0:?}")]
    InvalidManifest(#[source] anyhow::Error),
}

#[derive(Debug, Error)]
pub enum FacetError {
    #[error("Facet '{0}' defined but is null")]
    NullFacet(&'static str),

    #[error("Invalid facet: {0}, value: {1:?}, allowed value(s): {2}")]
    InvalidFacetValue(&'static str, String, String),
}

impl From<FacetError> for LaunchTestError {
    fn from(e: FacetError) -> Self {
        Self::InvalidManifest(e.into())
    }
}

impl From<LaunchTestError> for LaunchError {
    fn from(e: LaunchTestError) -> Self {
        match e {
            LaunchTestError::CreateProxyForArchiveAccessor(_)
            | LaunchTestError::InitializeTestRealm(_)
            | LaunchTestError::ConnectToArchiveAccessor(_)
            | LaunchTestError::StreamIsolatedLogs(_)
            | LaunchTestError::InvalidResolverData
            | LaunchTestError::InvalidManifest(_)
            | LaunchTestError::ManifestIo(_) => Self::InternalError,
            LaunchTestError::CreateTestRealm(_) | LaunchTestError::ResolveTest(_) => {
                Self::InstanceCannotResolve
            }
            LaunchTestError::ConnectToTestSuite(_) => Self::FailedToConnectToTestSuite,
        }
    }
}
