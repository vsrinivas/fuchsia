// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    clonable_error::ClonableError,
    failure::{Error, Fail},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::{future::BoxFuture, stream::TryStreamExt},
};

/// Executes a component instance.
/// TODO: The runner should return a trait object to allow the component instance to be stopped,
/// binding to services, and observing abnormal termination.  In other words, a wrapper that
/// encapsulates fsys::ComponentController FIDL interfacing concerns.
/// TODO: Consider defining an internal representation for `fsys::ComponentStartInfo` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
pub trait Runner {
    fn start(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> BoxFuture<Result<(), RunnerError>>;
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
    #[fail(display = "runner failed to launch component with url \"{}\": {}", url, err)]
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
    #[fail(display = "failed to connect to the runner: {}", err)]
    RunnerConnectionError {
        #[fail(cause)]
        err: ClonableError,
    },
    #[fail(display = "remote runners unsupported")]
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

    /// Convert this error into its approximate fsys::Error equivalent.
    pub fn as_fidl_error(&self) -> fsys::Error {
        match self {
            RunnerError::InvalidArgs { .. } => fsys::Error::InvalidArguments,
            RunnerError::ComponentLoadError { .. } => fsys::Error::InstanceCannotStart,
            RunnerError::ComponentLaunchError { .. } => fsys::Error::InstanceCannotStart,
            RunnerError::ComponentRuntimeDirectoryError { .. } => fsys::Error::Internal,
            RunnerError::RunnerConnectionError { .. } => fsys::Error::Internal,
            RunnerError::Unsupported { .. } => fsys::Error::Unsupported,
        }
    }
}

/// A null runner for components without a runtime environment.
///
/// Such environments, even though they don't execute any code, can still be
/// used by other components to bind to, which in turn may trigger further
/// bindings to its children.
pub(super) struct NullRunner {}

impl Runner for NullRunner {
    fn start(
        &self,
        _start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> BoxFuture<Result<(), RunnerError>> {
        spawn_null_controller_server(
            server_end
                .into_stream()
                .expect("NullRunner failed to convert server channel into request stream"),
        );
        Box::pin(async { Ok(()) })
    }
}

/// Spawn an async execution context which takes ownership of `server_end`
/// and holds on to it until a stop or kill request is received.
fn spawn_null_controller_server(mut request_stream: fsys::ComponentControllerRequestStream) {
    // Listen to the ComponentController server end and exit after the first
    // one, as this is the contract we have implemented so far. Exiting will
    // cause our handle to the channel to drop and close the channel.
    fasync::spawn(async move {
        while let Ok(Some(request)) = request_stream.try_next().await {
            match request {
                fsys::ComponentControllerRequest::Stop { control_handle: c } => {
                    c.shutdown();
                    break;
                }
                fsys::ComponentControllerRequest::Kill { control_handle: c } => {
                    c.shutdown();
                    break;
                }
            }
        }
    });
}

/// Wrapper for converting fsys::Error into the failure::Error type.
#[derive(Debug, Clone, Fail)]
pub struct RemoteRunnerError(pub fsys::Error);

impl std::fmt::Display for RemoteRunnerError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // Use the Debug formatter for Display.
        use std::fmt::Debug;
        self.0.fmt(f)
    }
}

impl std::convert::From<fsys::Error> for RemoteRunnerError {
    fn from(error: fsys::Error) -> RemoteRunnerError {
        RemoteRunnerError(error)
    }
}

/// A runner provided by another component.
pub struct RemoteRunner {
    client: fsys::ComponentRunnerProxy,
}

impl RemoteRunner {
    pub fn new(client: fsys::ComponentRunnerProxy) -> RemoteRunner {
        RemoteRunner { client }
    }

    async fn start_async(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> Result<(), RunnerError> {
        let url = start_info.resolved_url.clone().unwrap_or_else(|| "<none>".to_string());
        self.client
            .start(start_info, server_end)
            .await
            .map_err(|e| RunnerError::runner_connection_error(e))?
            .map_err(|e| RunnerError::component_launch_error(url, RemoteRunnerError(e)))?;
        Ok(())
    }
}

impl Runner for RemoteRunner {
    fn start(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> BoxFuture<Result<(), RunnerError>> {
        Box::pin(self.start_async(start_info, server_end))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::endpoints,
        fidl_fuchsia_sys2 as fsys,
        fuchsia_async::OnSignals,
        fuchsia_zircon::{AsHandleRef, Signals},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_null_runner() {
        let null_runner = NullRunner {};
        let (client, server) =
            endpoints::create_endpoints::<fsys::ComponentControllerMarker>().unwrap();
        null_runner.start(
            fsys::ComponentStartInfo {
                resolved_url: None,
                program: None,
                ns: None,
                outgoing_dir: None,
                runtime_dir: None,
            },
            server,
        );
        let proxy = client.into_proxy().expect("failed converting to proxy");
        proxy.stop().expect("failed to send message to null runner");

        OnSignals::new(&proxy.as_handle_ref(), Signals::CHANNEL_PEER_CLOSED)
            .await
            .expect("failed waiting for channel to close");
    }
}
