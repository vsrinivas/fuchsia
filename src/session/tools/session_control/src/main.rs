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

#[derive(FromArgs, Debug, PartialEq)]
/// Various operations to control sessions.
pub struct Args {
    #[argh(subcommand)]
    pub command: Command,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Command {
    Launch(LaunchCommand),
    Restart(RestartCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "launch")]
/// Launch a new session.
pub struct LaunchCommand {
    /// the URL for the session to launch.
    #[argh(positional)]
    pub session_url: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "restart")]
/// Restart the current session.
pub struct RestartCommand {}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Args { command } = argh::from_env();
    let launcher = connect_to_service::<LauncherMarker>()?;

    match command {
        Command::Launch(LaunchCommand { session_url }) => {
            match launch_session(&session_url, launcher).await {
                Ok(_) => println!("Launched session: {:?}", session_url),
                Err(err) => println!("Failed to launch session: {:?}, {:?}", session_url, err),
            };
        }
        Command::Restart(RestartCommand {}) => {
            match restart_session(launcher).await {
                Ok(_) => println!("Restarted the session."),
                Err(err) => println!("Failed to restart session: {:?}", err),
            };
        }
    };

    Ok(())
}

/// Launches a session.
///
/// # Parameters
/// - `session_url`: The URL of the session to launch.
/// - `launcher`: The launcher proxy to use to launch the session.
///
/// # Errors
/// Returns an error if there is an issue launching the session.
async fn launch_session(session_url: &str, launcher: LauncherProxy) -> Result<(), Error> {
    let result = launcher
        .launch_session(SessionConfiguration { session_url: Some(session_url.to_string()) })
        .await?;
    result.map_err(|err: fidl_fuchsia_session::LaunchSessionError| format_err!("{:?}", err))?;
    Ok(())
}

/// Restarts a session.
///
/// # Parameters
/// - `launcher`: The launcher proxy to use to launch the session.
///
/// # Errors
/// Returns an error if there is either an issue launching the session or
/// there isn't a previous session to be restarted.
async fn restart_session(launcher: LauncherProxy) -> Result<(), Error> {
    let result = launcher.restart_session().await?;
    result.map_err(|err: fidl_fuchsia_session::LaunchSessionError| format_err!("{:?}", err))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::{LaunchSessionError, LauncherMarker, LauncherRequest},
        // fidl_fuchsia.sys2::EventType, TODO(fxbug.dev/47730): re-enable the tests.
        fuchsia_async as fasync,
        futures::TryStreamExt,
        // test_utils_lib::events::{EventSource, Ordering, RecordedEvent},
    };

    /// Tests that the session_control tool successfully handles a call to LaunchSession with the provided URL.
    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        fasync::Task::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                if let LauncherRequest::LaunchSession { configuration, responder } = launch_request
                {
                    assert_eq!(configuration.session_url, Some(session_url.to_string()));
                    let _ = responder.send(&mut Ok(()));
                } else {
                    assert!(false);
                }
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(launch_session(&session_url, launcher).await.is_ok());
    }

    /// Tests that the session_control tool returns an error on a call to LaunchSession when there
    ///  is no SessionConfiguration provided.
    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session_error() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        fasync::Task::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                if let LauncherRequest::LaunchSession { configuration: _, responder } =
                    launch_request
                {
                    let _ = responder.send(&mut Err(LaunchSessionError::Failed));
                } else {
                    assert!(false);
                }
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(launch_session(&session_url, launcher).await.is_err());
    }
    /// Tests that restart_session makes the appropriate calls to fuchsia.session.Launcher.
    #[fasync::run_singlethreaded(test)]
    async fn test_restart_session() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");

        fasync::Task::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                if let LauncherRequest::RestartSession { responder } = launch_request {
                    let _ = responder.send(&mut Ok(()));
                } else {
                    assert!(false);
                }
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(restart_session(launcher).await.is_ok());
    }

    /// Tests that restart_session returns an error when an error is returned from fuchsia.sys.Launcher.
    #[fasync::run_singlethreaded(test)]
    async fn test_restart_session_error() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");

        fasync::Task::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                if let LauncherRequest::RestartSession { responder } = launch_request {
                    let _ = responder.send(&mut Err(LaunchSessionError::NotFound));
                } else {
                    assert!(false);
                }
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(restart_session(launcher).await.is_err());
    }

    // TODO(fxbug.dev/47730): re-enable these tests.
    // /// Verifies that session control is routed the expected capabilities.
    // #[fasync::run_singlethreaded(test)]
    // async fn test_capability_routing() {
    //     let event_source = EventSource::new().unwrap();
    //     event_source.start_component_tree().await.unwrap();

    //     let launcher = connect_to_service::<LauncherMarker>().expect("launcher service?");

    //     let expected_events = vec![
    //         RecordedEvent {
    //             event_type: EventType::CapabilityRouted,
    //             target_moniker: "./session:session:*".to_string(),
    //             capability_id: Some("elf".to_string()),
    //         },
    //         RecordedEvent {
    //             event_type: EventType::CapabilityRouted,
    //             target_moniker: "./session:session:*".to_string(),
    //             capability_id: Some("/svc/fuchsia.session.Launcher".to_string()),
    //         },
    //     ];

    //     let expectation =
    //         event_source.expect_events(Ordering::Unordered, expected_events).await.unwrap();

    //     let session_url = "fuchsia-pkg://fuchsia.com/session_control#meta/session_control.cm";

    //     match launch_session(&session_url, launcher).await {
    //         Ok(_) => fx_log_info!("Launched session: {:?}", session_url),
    //         Err(err) => fx_log_info!("Failed to launch session: {:?}, {:?}", session_url, err),
    //     };

    //     expectation.await.unwrap();
    // }

    // /// Verifies that the session is correctly resolved and launched without errors.
    // #[fasync::run_singlethreaded(test)]
    // async fn test_session_lifecycle() {
    //     let event_source = EventSource::new().unwrap();
    //     event_source.start_component_tree().await.unwrap();
    //     let launcher = connect_to_service::<LauncherMarker>().expect("launcher service?");

    //     let expected_events = vec![
    //         RecordedEvent {
    //             event_type: EventType::Resolved,
    //             target_moniker: "./session:session:*".to_string(),
    //             capability_id: None,
    //         },
    //         RecordedEvent {
    //             event_type: EventType::Started,
    //             target_moniker: "./session:session:*".to_string(),
    //             capability_id: None,
    //         },
    //     ];

    //     let expectation =
    //         event_source.expect_events(Ordering::Ordered, expected_events).await.unwrap();

    //     let session_url = "fuchsia-pkg://fuchsia.com/session_control#meta/session_control.cm";
    //     match launch_session(&session_url, launcher).await {
    //         Ok(_) => println!("Launched session: {:?}", session_url),
    //         Err(err) => println!("Failed to launch session: {:?}, {:?}", session_url, err),
    //     };

    //     expectation.await.unwrap();
    // }
}
