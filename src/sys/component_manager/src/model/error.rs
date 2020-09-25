// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        environment::EnvironmentError,
        events::error::EventsError,
        moniker::{AbsoluteMoniker, PartialMoniker},
        resolver::ResolverError,
        rights::RightsError,
        routing::RoutingError,
        runner::RunnerError,
        storage::StorageError,
    },
    anyhow::Error,
    clonable_error::ClonableError,
    std::{ffi::OsString, path::PathBuf},
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
    #[error("environment {} not found in realm {}", name, moniker)]
    EnvironmentNotFound { name: String, moniker: AbsoluteMoniker },
    #[error("environment {} in realm {} is not valid: {}", name, moniker, err)]
    EnvironmentInvalid {
        name: String,
        moniker: AbsoluteMoniker,
        #[source]
        err: EnvironmentError,
    },
    #[error("{} is not supported", feature)]
    Unsupported { feature: String },
    #[error("component declaration invalid")]
    ComponentInvalid,
    #[error("path is not utf-8: {:?}", path)]
    PathIsNotUtf8 { path: PathBuf },
    #[error("path is not valid: {:?}", path)]
    PathInvalid { path: String },
    #[error("filename is not utf-8: {:?}", name)]
    NameIsNotUtf8 { name: OsString },
    #[error("component manifest invalid {}: {}", url, err)]
    ManifestInvalid {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("The model is not available")]
    ModelNotAvailable,
    #[error("Namespace creation failed: {}", err)]
    NamespaceCreationFailed {
        #[source]
        err: ClonableError,
    },
    #[error("Resolver error: {}", err)]
    ResolverError {
        #[source]
        err: ResolverError,
    },
    #[error("Routing error: {}", err)]
    RoutingError {
        #[source]
        err: RoutingError,
    },
    #[error("Runner error: {}", err)]
    RunnerError {
        #[source]
        err: RunnerError,
    },
    #[error("storage error: {}", err)]
    StorageError {
        #[source]
        err: StorageError,
    },
    #[error("failed to add entry {} to {}", entry_name, moniker)]
    AddEntryError { moniker: AbsoluteMoniker, entry_name: String },
    #[error("failed to remove entry {}", entry_name)]
    RemoveEntryError { entry_name: String },
    #[error("failed to open directory '{}' for component '{}'", relative_path, moniker)]
    OpenDirectoryError { moniker: AbsoluteMoniker, relative_path: String },
    #[error("failed to create stream from channel")]
    StreamCreationError {
        #[source]
        err: ClonableError,
    },
    #[error("insufficient resources to complete operation")]
    InsufficientResources,
    #[error("failed to send {} to runner for component {}", operation, moniker)]
    RunnerCommunicationError {
        moniker: AbsoluteMoniker,
        operation: String,
        #[source]
        err: ClonableError,
    },
    #[error("rights error")]
    RightsError {
        #[source]
        err: RightsError,
    },
    #[error("events error")]
    EventsError {
        #[source]
        err: EventsError,
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

    pub fn environment_not_found(name: impl Into<String>, moniker: AbsoluteMoniker) -> ModelError {
        ModelError::EnvironmentNotFound { name: name.into(), moniker }
    }

    pub fn unsupported(feature: impl Into<String>) -> ModelError {
        ModelError::Unsupported { feature: feature.into() }
    }

    pub fn path_is_not_utf8(path: PathBuf) -> ModelError {
        ModelError::PathIsNotUtf8 { path }
    }

    pub fn path_invalid(path: impl Into<String>) -> ModelError {
        ModelError::PathInvalid { path: path.into() }
    }

    pub fn name_is_not_utf8(name: OsString) -> ModelError {
        ModelError::NameIsNotUtf8 { name }
    }

    pub fn namespace_creation_failed(err: impl Into<Error>) -> ModelError {
        ModelError::NamespaceCreationFailed { err: err.into().into() }
    }

    pub fn manifest_invalid(url: impl Into<String>, err: impl Into<Error>) -> ModelError {
        ModelError::ManifestInvalid { url: url.into(), err: err.into().into() }
    }

    pub fn model_not_available() -> ModelError {
        ModelError::ModelNotAvailable
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

    pub fn stream_creation_error(err: impl Into<Error>) -> ModelError {
        ModelError::StreamCreationError { err: err.into().into() }
    }
}

impl From<RightsError> for ModelError {
    fn from(err: RightsError) -> Self {
        ModelError::RightsError { err }
    }
}

impl From<ResolverError> for ModelError {
    fn from(err: ResolverError) -> Self {
        ModelError::ResolverError { err }
    }
}

impl From<RoutingError> for ModelError {
    fn from(err: RoutingError) -> Self {
        ModelError::RoutingError { err }
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

impl From<EventsError> for ModelError {
    fn from(err: EventsError) -> Self {
        ModelError::EventsError { err }
    }
}
