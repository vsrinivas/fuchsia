// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, async_trait::async_trait, clonable_error::ClonableError,
    fidl::endpoints::ServerEnd, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::stream::TryStreamExt, thiserror::Error,
};

/// Executes a component instance.
/// TODO: The runner should return a trait object to allow the component instance to be stopped,
/// binding to services, and observing abnormal termination.  In other words, a wrapper that
/// encapsulates fcrunner::ComponentController FIDL interfacing concerns.
/// TODO: Consider defining an internal representation for `fcrunner::ComponentStartInfo` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
#[async_trait]
pub trait Runner: Sync + Send {
    #[must_use]
    async fn start(
        &self,
        start_info: fcrunner::ComponentStartInfo,
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    );
}

/// Errors produced by `Runner`.
#[derive(Debug, Error, Clone)]
pub enum RunnerError {
    #[error("invalid arguments provided for component with url \"{}\": {}", url, err)]
    InvalidArgs {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("unable to load component with url \"{}\": {}", url, err)]
    ComponentLoadError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("runner failed to launch component with url \"{}\": {}", url, err)]
    ComponentLaunchError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to populate the runtime directory: {}", err)]
    ComponentRuntimeDirectoryError {
        #[source]
        err: ClonableError,
    },
    #[error("failed to connect to the runner: {}", err)]
    RunnerConnectionError {
        #[source]
        err: ClonableError,
    },
    #[error("remote runners unsupported")]
    Unsupported,
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

    pub fn runner_connection_error(err: impl Into<Error>) -> RunnerError {
        RunnerError::RunnerConnectionError { err: err.into().into() }
    }

    /// Convert this error into its approximate `fuchsia.component.Error` equivalent.
    pub fn as_fidl_error(&self) -> fcomponent::Error {
        match self {
            RunnerError::InvalidArgs { .. } => fcomponent::Error::InvalidArguments,
            RunnerError::ComponentLoadError { .. } => fcomponent::Error::InstanceCannotStart,
            RunnerError::ComponentLaunchError { .. } => fcomponent::Error::InstanceCannotStart,
            RunnerError::ComponentRuntimeDirectoryError { .. } => fcomponent::Error::Internal,
            RunnerError::RunnerConnectionError { .. } => fcomponent::Error::Internal,
            RunnerError::Unsupported { .. } => fcomponent::Error::Unsupported,
        }
    }

    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            RunnerError::InvalidArgs { .. } => zx::Status::INVALID_ARGS,
            RunnerError::ComponentLoadError { .. } => zx::Status::UNAVAILABLE,
            RunnerError::ComponentLaunchError { .. } => zx::Status::UNAVAILABLE,
            RunnerError::ComponentRuntimeDirectoryError { .. } => zx::Status::INTERNAL,
            RunnerError::RunnerConnectionError { .. } => zx::Status::INTERNAL,
            RunnerError::Unsupported { .. } => zx::Status::NOT_SUPPORTED,
        }
    }
}

/// A null runner for components without a runtime environment.
///
/// Such environments, even though they don't execute any code, can still be
/// used by other components to bind to, which in turn may trigger further
/// bindings to its children.
pub(super) struct NullRunner {}

#[async_trait]
impl Runner for NullRunner {
    async fn start(
        &self,
        _start_info: fcrunner::ComponentStartInfo,
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) {
        spawn_null_controller_server(
            server_end
                .into_stream()
                .expect("NullRunner failed to convert server channel into request stream"),
        );
    }
}

/// Spawn an async execution context which takes ownership of `server_end`
/// and holds on to it until a stop or kill request is received.
fn spawn_null_controller_server(mut request_stream: fcrunner::ComponentControllerRequestStream) {
    // Listen to the ComponentController server end and exit after the first
    // one, as this is the contract we have implemented so far. Exiting will
    // cause our handle to the channel to drop and close the channel.
    fasync::Task::spawn(async move {
        while let Ok(Some(request)) = request_stream.try_next().await {
            match request {
                fcrunner::ComponentControllerRequest::Stop { control_handle: c } => {
                    c.shutdown();
                    break;
                }
                fcrunner::ComponentControllerRequest::Kill { control_handle: c } => {
                    c.shutdown();
                    break;
                }
            }
        }
    })
    .detach();
}

/// Wrapper for converting fcomponent::Error into the anyhow::Error type.
#[derive(Debug, Clone, Error)]
pub struct RemoteRunnerError(pub fcomponent::Error);

impl std::fmt::Display for RemoteRunnerError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // Use the Debug formatter for Display.
        use std::fmt::Debug;
        self.0.fmt(f)
    }
}

impl std::convert::From<fcomponent::Error> for RemoteRunnerError {
    fn from(error: fcomponent::Error) -> RemoteRunnerError {
        RemoteRunnerError(error)
    }
}

/// A runner provided by another component.
pub struct RemoteRunner {
    client: fcrunner::ComponentRunnerProxy,
}

impl RemoteRunner {
    pub fn new(client: fcrunner::ComponentRunnerProxy) -> RemoteRunner {
        RemoteRunner { client }
    }
}

#[async_trait]
impl Runner for RemoteRunner {
    async fn start(
        &self,
        start_info: fcrunner::ComponentStartInfo,
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) {
        self.client.start(start_info, server_end).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{self, Proxy};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_null_runner() {
        let null_runner = NullRunner {};
        let (client, server) =
            endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        null_runner
            .start(
                fcrunner::ComponentStartInfo {
                    resolved_url: None,
                    program: None,
                    ns: None,
                    outgoing_dir: None,
                    runtime_dir: None,
                },
                server,
            )
            .await;
        let proxy = client.into_proxy().expect("failed converting to proxy");
        proxy.stop().expect("failed to send message to null runner");

        proxy.on_closed().await.expect("failed waiting for channel to close");
    }
}
