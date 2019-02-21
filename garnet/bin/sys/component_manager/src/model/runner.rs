// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, Fail},
    fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
};

/// Executes a component instance.
/// TODO: The runner should return a trait object to allow the component instance to be stopped,
/// binding to services, and observing abnormal termination.  In other words, a wrapper that
/// encapsulates fsys::ComponentController FIDL interfacing concerns.
/// TODO: Consider defining an internal representation for `fsys::ComponentStartInfo` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
pub trait Runner {
    fn start(&self, start_info: fsys::ComponentStartInfo) -> FutureObj<Result<(), RunnerError>>;
}

/// Errors produced by `Runner`.
#[derive(Debug, Fail)]
pub enum RunnerError {
    #[fail(display = "invalid arguments provided for component with uri \"{}\": {}", uri, err)]
    InvalidArgs {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "unable to load component with uri \"{}\": {}", uri, err)]
    ComponentLoadError {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "failed to launch component with uri \"{}\": {}", uri, err)]
    ComponentLaunchError {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
}

impl RunnerError {
    pub fn invalid_args(uri: impl Into<String>, err: impl Into<Error>) -> RunnerError {
        RunnerError::InvalidArgs { uri: uri.into(), err: err.into() }
    }

    pub fn component_load_error(uri: impl Into<String>, err: impl Into<Error>) -> RunnerError {
        RunnerError::ComponentLoadError { uri: uri.into(), err: err.into() }
    }

    pub fn component_launch_error(uri: impl Into<String>, err: impl Into<Error>) -> RunnerError {
        RunnerError::ComponentLaunchError { uri: uri.into(), err: err.into() }
    }
}
