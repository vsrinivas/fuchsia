// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    clonable_error::ClonableError,
    failure::{Error, Fail},
    fidl_fuchsia_sys2 as fsys,
    futures::future::BoxFuture,
};

/// Executes a component instance.
/// TODO: The runner should return a trait object to allow the component instance to be stopped,
/// binding to services, and observing abnormal termination.  In other words, a wrapper that
/// encapsulates fsys::ComponentController FIDL interfacing concerns.
/// TODO: Consider defining an internal representation for `fsys::ComponentStartInfo` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
pub trait Runner {
    fn start(&self, start_info: fsys::ComponentStartInfo) -> BoxFuture<Result<(), RunnerError>>;
}

/// Errors produced by `Runner`.
#[derive(Debug, Fail, Clone)]
pub enum RunnerError {
    #[fail(display = "invalid arguments provided for component with url \"{}\": {}", url, err)]
    InvalidArgs {
        url: String,
        #[fail(cause)]
        err: ClonableError,
    },
    #[fail(display = "unable to load component with url \"{}\": {}", url, err)]
    ComponentLoadError {
        url: String,
        #[fail(cause)]
        err: ClonableError,
    },
    #[fail(display = "failed to launch component with url \"{}\": {}", url, err)]
    ComponentLaunchError {
        url: String,
        #[fail(cause)]
        err: ClonableError,
    },
    #[fail(display = "failed to populate the runtime directory: {}", err)]
    ComponentRuntimeDirectoryError {
        #[fail(cause)]
        err: ClonableError,
    },
}

impl RunnerError {
    pub fn invalid_args(url: impl Into<String>, err: impl Into<Error>) -> RunnerError {
        RunnerError::InvalidArgs { url: url.into(), err: err.into().into() }
    }

    pub fn component_load_error(url: impl Into<String>, err: impl Into<Error>) -> RunnerError {
        RunnerError::ComponentLoadError { url: url.into(), err: err.into().into() }
    }

    pub fn component_launch_error(url: impl Into<String>, err: impl Into<Error>) -> RunnerError {
        RunnerError::ComponentLaunchError { url: url.into(), err: err.into().into() }
    }

    pub fn component_runtime_directory_error(err: impl Into<Error>) -> RunnerError {
        RunnerError::ComponentRuntimeDirectoryError { err: err.into().into() }
    }
}

/// A null runner for components without a runtime environment.
///
/// Such environments, even though they don't execute any code, can still be
/// used by other components to bind to, which in turn may trigger further
/// bindings to its children.
pub struct NullRunner {}

impl Runner for NullRunner {
    fn start(&self, _start_info: fsys::ComponentStartInfo) -> BoxFuture<Result<(), RunnerError>> {
        Box::pin(async { Ok(()) })
    }
}
