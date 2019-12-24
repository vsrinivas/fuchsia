// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_fuchsia_session::{LauncherMarker, LauncherProxy, SessionConfiguration},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[derive(FromArgs)]
/// The session control component.
pub struct SessionControlArgs {
    #[argh(option, short = 's')]
    /// the URL for the session to launch.
    pub session_url: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let SessionControlArgs { session_url } = argh::from_env();
    let launcher = connect_to_service::<LauncherMarker>()?;

    match launch_session(&session_url, launcher).await {
        Ok(_) => println!("Launched session: {:?}", session_url),
        Err(err) => println!("Failed to launch session: {:?}, {:?}", session_url, err),
    };

    Ok(())
}

/// Launches a session.
///
/// # Parameters
/// - `session_url`: The URL of the session to launch.
/// - `launcher`: The launcher proxy to use to launch the session.
async fn launch_session(session_url: &str, launcher: LauncherProxy) -> Result<(), Error> {
    let result = launcher
        .launch_session(SessionConfiguration { session_url: Some(session_url.to_string()) })
        .await?;
    result.map_err(|err: fidl_fuchsia_session::LaunchSessionError| format_err!("{:?}", err))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::launch_session,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::{LaunchSessionError, LauncherMarker, LauncherRequest},
        fuchsia_async as fasync,
        futures::TryStreamExt,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        fasync::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                let LauncherRequest::LaunchSession { configuration, responder } = launch_request;
                assert_eq!(configuration.session_url, Some(session_url.to_string()));
                let _ = responder.send(&mut Ok(()));
            } else {
                assert!(false);
            }
        });

        assert!(launch_session(&session_url, launcher).await.is_ok());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session_error() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        fasync::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                let LauncherRequest::LaunchSession { configuration: _, responder } = launch_request;
                let _ = responder.send(&mut Err(LaunchSessionError::Failed));
            } else {
                assert!(false);
            }
        });

        assert!(launch_session(&session_url, launcher).await.is_err());
    }
}
