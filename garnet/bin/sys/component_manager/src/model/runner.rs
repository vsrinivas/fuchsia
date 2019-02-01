// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
    std::{error, fmt},
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
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RunnerError {
    ComponentNotAvailable,
    InvalidArgs,
}

impl fmt::Display for RunnerError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            RunnerError::ComponentNotAvailable => write!(f, "component not available"),
            RunnerError::InvalidArgs => write!(f, "invalid args"),
        }
    }
}

impl error::Error for RunnerError {
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        match *self {
            RunnerError::ComponentNotAvailable => None,
            RunnerError::InvalidArgs => None,
        }
    }
}
