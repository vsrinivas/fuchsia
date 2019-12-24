// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        moniker::{AbsoluteMoniker, PartialMoniker},
        resolver::ResolverError,
        runner::RunnerError,
        storage::StorageError,
    },
    anyhow::Error,
    clonable_error::ClonableError,
    thiserror::Error,
};

/// Errors produced by `Model`.
#[derive(Debug, Error, Clone)]
pub enum ModelError {
    #[error("component instance {} not found in realm {}", child, moniker)]
    InstanceNotFoundInRealm { moniker: AbsoluteMoniker, child: PartialMoniker },
    #[error("component instance {} in realm {} already exists", child, moniker)]
    InstanceAlreadyExists { moniker: AbsoluteMoniker, child: PartialMoniker },
    #[error("component instance with moniker {} has shut down", moniker)]
    InstanceShutDown { moniker: AbsoluteMoniker },
    #[error("component instance {} not found", moniker)]
    InstanceNotFound { moniker: AbsoluteMoniker },
    #[error("component collection not found with name {}", name)]
    CollectionNotFound { name: String },
    #[error("{} is not supported", feature)]
    Unsupported { feature: String },
    #[error("component declaration invalid")]
    ComponentInvalid,
    #[error("component manifest invalid")]
    ManifestInvalid {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("The model is not available")]
    ModelNotAvailable,
    #[error("namespace creation failed: {}", err)]
    NamespaceCreationFailed {
        #[source]
        err: ClonableError,
    },
    #[error("resolver error")]
    ResolverError {
        #[source]
        err: ResolverError,
    },
    #[error("runner error: {}", err)]
    RunnerError {
        #[source]
        err: RunnerError,
    },
    #[error("capability discovery error")]
    CapabilityDiscoveryError {
        #[source]
        err: ClonableError,
    },
    #[error("storage error")]
    StorageError {
        #[source]
        err: StorageError,
    },
    #[error("failed to add entry {} to {}", entry_name, moniker)]
    AddEntryError { moniker: AbsoluteMoniker, entry_name: String },
    #[error("failed to remove entry {}", entry_name)]
    RemoveEntryError { entry_name: String },
    #[error("open directory error")]
    OpenDirectoryError { moniker: AbsoluteMoniker, relative_path: String },
    #[error("insufficient resources to complete operation")]
    InsufficientResources,
    #[error("failed to send {} to runner for component {}", operation, moniker)]
    RunnerCommunicationError {
        moniker: AbsoluteMoniker,
        operation: String,
        #[source]
        err: ClonableError,
    },
}

impl ModelError {
    pub fn instance_not_found_in_realm(
        moniker: AbsoluteMoniker,
        child: PartialMoniker,
    ) -> ModelError {
        ModelError::InstanceNotFoundInRealm { moniker, child }
    }

    pub fn instance_already_exists(moniker: AbsoluteMoniker, child: PartialMoniker) -> ModelError {
        ModelError::InstanceAlreadyExists { moniker, child }
    }

    pub fn instance_shut_down(moniker: AbsoluteMoniker) -> ModelError {
        ModelError::InstanceShutDown { moniker }
    }

    pub fn instance_not_found(moniker: AbsoluteMoniker) -> ModelError {
        ModelError::InstanceNotFound { moniker }
    }

    pub fn collection_not_found(name: impl Into<String>) -> ModelError {
        ModelError::CollectionNotFound { name: name.into() }
    }

    pub fn unsupported(feature: impl Into<String>) -> ModelError {
        ModelError::Unsupported { feature: feature.into() }
    }

    pub fn namespace_creation_failed(err: impl Into<Error>) -> ModelError {
        ModelError::NamespaceCreationFailed { err: err.into().into() }
    }

    pub fn manifest_invalid(url: impl Into<String>, err: impl Into<Error>) -> ModelError {
        ModelError::ManifestInvalid { url: url.into(), err: err.into().into() }
    }

    pub fn capability_discovery_error(err: impl Into<Error>) -> ModelError {
        ModelError::CapabilityDiscoveryError { err: err.into().into() }
    }

    pub fn add_entry_error(moniker: AbsoluteMoniker, entry_name: impl Into<String>) -> ModelError {
        ModelError::AddEntryError { moniker, entry_name: entry_name.into() }
    }

    pub fn remove_entry_error(entry_name: impl Into<String>) -> ModelError {
        ModelError::RemoveEntryError { entry_name: entry_name.into() }
    }

    pub fn open_directory_error(
        moniker: AbsoluteMoniker,
        relative_path: impl Into<String>,
    ) -> ModelError {
        ModelError::OpenDirectoryError { moniker, relative_path: relative_path.into() }
    }
}

impl From<ResolverError> for ModelError {
    fn from(err: ResolverError) -> Self {
        ModelError::ResolverError { err }
    }
}

impl From<RunnerError> for ModelError {
    fn from(err: RunnerError) -> Self {
        ModelError::RunnerError { err }
    }
}

impl From<StorageError> for ModelError {
    fn from(err: StorageError) -> Self {
        ModelError::StorageError { err }
    }
}
