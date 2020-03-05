// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::startup,
    anyhow::{Context as _, Error},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_session::{LaunchSessionError, LauncherRequest, LauncherRequestStream},
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

/// The services exposed by the session manager.
enum ExposedServices {
    Launcher(LauncherRequestStream),
}

/// Starts serving [`ExposedServices`] from `svc`.
///
/// This will return once the [`ServiceFs`] stops serving requests.
///
/// # Parameters
/// - `initial_url`: The initial URL of the launched session.
///
/// # Errors
/// Returns an error if there is an issue serving the `svc` directory handle.
pub async fn expose_services(initial_url: &mut String) -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Launcher);
    fs.take_and_serve_directory_handle()?;
    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::Launcher(request_stream) => {
                handle_session_manager_request_stream(request_stream, initial_url)
                    .await
                    .expect("Session launcher request stream got an error.");
            }
        }
    }

    Ok(())
}

/// Handles calls to launch_session() and returns a Result containing
/// the most recently launched session url or a LaunchSessionError.
///
/// # Parameters
/// - session_url: An optional session url.
async fn handle_launch_session_request(
    session_url: Option<String>,
) -> Result<String, LaunchSessionError> {
    if let Some(session_url) = session_url {
        match startup::launch_session(&session_url).await {
            Ok(_) => return Ok(session_url),
            Err(err) => match err {
                startup::StartupError::NotCreated {
                    name: _,
                    collection: _,
                    url: _,
                    err: sys_err,
                } => match sys_err {
                    fcomponent::Error::InstanceCannotResolve => {
                        return Err(LaunchSessionError::NotFound)
                    }
                    _ => return Err(LaunchSessionError::Failed),
                },
                _ => return Err(LaunchSessionError::Failed),
            },
        };
    } else {
        return Err(LaunchSessionError::NotFound);
    }
}

/// Serves a specified [`LauncherRequestStream`].
///
/// # Parameters
/// - `request_stream`: the LauncherRequestStream.
/// - `session_url`: the URL of the most recently launched session.
///
/// # Errors
/// When an error is encountered reading from the request stream.
async fn handle_session_manager_request_stream(
    mut request_stream: LauncherRequestStream,
    session_url: &mut String,
) -> Result<(), Error> {
    while let Some(request) =
        request_stream.try_next().await.context("Error handling launcher request stream")?
    {
        match request {
            LauncherRequest::LaunchSession { configuration, responder } => {
                let result = handle_launch_session_request(configuration.session_url).await;
                match result {
                    Ok(new_session_url) => {
                        let _ = responder.send(&mut Ok(()));
                        *session_url = new_session_url;
                    }
                    Err(err) => {
                        let _ = responder.send(&mut Err(err));
                    }
                }
            }
            LauncherRequest::RestartSession { responder } => {
                let result = handle_launch_session_request(Some(session_url.clone())).await;

                match result {
                    Ok(new_session_url) => {
                        let _ = responder.send(&mut Ok(()));
                        *session_url = new_session_url;
                    }
                    Err(err) => {
                        let _ = responder.send(&mut Err(err));
                    }
                }
            }
        };
    }
    Ok(())
}
