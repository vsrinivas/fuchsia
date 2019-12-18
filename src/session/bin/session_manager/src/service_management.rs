// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::startup,
    failure::{Error, ResultExt},
    fidl_fuchsia_session::{LaunchSessionError, LauncherRequest, LauncherRequestStream},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

/// The services exposed by the session manager.
enum ExposedServices {
    Launcher(LauncherRequestStream),
}

/// The number of concurrent requests to serve for [`ExposedServices`].
const NUM_CONCURRENT_REQUESTS: usize = 5;

/// Starts serving [`ExposedServices`] from `svc`.
///
/// This will serve `NUM_CONCURRENT_REQUESTS` concurrently, and return once the [`ServiceFs`] stops
/// serving requests.
///
/// # Errors
/// Returns an error if there is an issue serving the `svc` directory handle.
pub async fn expose_services() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Launcher);
    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(
        NUM_CONCURRENT_REQUESTS,
        move |service_request: ExposedServices| async move {
            match service_request {
                ExposedServices::Launcher(request_stream) => {
                    handle_session_manager_request_stream(request_stream)
                        .await
                        .expect("Session launcher request stream got an error.");
                }
            }
        },
    )
    .await;

    Ok(())
}

/// Serves a specified [`LauncherRequestStream`].
///
/// # Errors
/// When an error is encountered reading from the request stream.
async fn handle_session_manager_request_stream(
    mut request_stream: LauncherRequestStream,
) -> Result<(), Error> {
    while let Some(LauncherRequest::LaunchSession { configuration, responder }) =
        request_stream.try_next().await.context("Error handling launcher request stream")?
    {
        if let Some(session_url) = configuration.session_url {
            let mut response = match startup::launch_session(&session_url).await {
                Ok(_) => Ok(()),
                Err(err) => match err {
                    startup::StartupError::NotCreated {
                        name: _,
                        collection: _,
                        url: _,
                        err: sys_err,
                    } => match sys_err {
                        fsys::Error::InstanceCannotResolve => Err(LaunchSessionError::NotFound),
                        _ => Err(LaunchSessionError::Failed),
                    },
                    _ => Err(LaunchSessionError::Failed),
                },
            };
            let _ = responder.send(&mut response);
        } else {
            let _ = responder.send(&mut Err(LaunchSessionError::NotFound));
        }
    }
    Ok(())
}
