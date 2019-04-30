// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    failure::{Error, Fail},
};

/// Errors produced by `Model`.
#[derive(Debug, Fail)]
pub enum ModelError {
    #[fail(display = "component instance not found with moniker {}", moniker)]
    InstanceNotFound { moniker: AbsoluteMoniker },
    #[fail(display = "component declaration invalid")]
    ComponentInvalid,
    #[fail(display = "component manifest invalid")]
    ManifestInvalid {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "namespace creation failed: {}", err)]
    NamespaceCreationFailed {
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "resolver error")]
    ResolverError {
        #[fail(cause)]
        err: ResolverError,
    },
    #[fail(display = "runner error")]
    RunnerError {
        #[fail(cause)]
        err: RunnerError,
    },
    #[fail(display = "capability discovery error")]
    CapabilityDiscoveryError {
        #[fail(cause)]
        err: Error,
    },
}

impl ModelError {
    pub fn instance_not_found(moniker: AbsoluteMoniker) -> ModelError {
        ModelError::InstanceNotFound { moniker }
    }

    pub fn namespace_creation_failed(err: impl Into<Error>) -> ModelError {
        ModelError::NamespaceCreationFailed { err: err.into() }
    }

    pub fn manifest_invalid(uri: impl Into<String>, err: impl Into<Error>) -> ModelError {
        ModelError::ManifestInvalid { uri: uri.into(), err: err.into() }
    }

    pub fn capability_discovery_error(err: impl Into<Error>) -> ModelError {
        ModelError::CapabilityDiscoveryError { err: err.into() }
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
