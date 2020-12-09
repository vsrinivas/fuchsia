// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl::encoding::Decodable,
    fidl_fuchsia_session::{ElementManagerMarker, ElementManagerProxy, ElementSpec},
    fidl_fuchsia_session::{
        LaunchConfiguration, LauncherMarker, LauncherProxy, RestarterMarker, RestarterProxy,
    },
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
    Add(AddCommand),
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

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "add")]
/// Add an element to the current session.
pub struct AddCommand {
    /// the URL for the element to add.
    #[argh(positional)]
    pub element_url: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Args { command } = argh::from_env();
    match command {
        Command::Launch(LaunchCommand { session_url }) => {
            let launcher = connect_to_service::<LauncherMarker>()?;
            match launch_session(&session_url, launcher).await {
                Ok(_) => {
                    println!("Launched session: {:?}", session_url);
                    Ok(())
                }
                Err(err) => {
                    println!("Failed to launch session: {:?}, {:?}", session_url, err);
                    Err(err)
                }
            }
        }
        Command::Restart(RestartCommand {}) => {
            let restarter = connect_to_service::<RestarterMarker>()?;
            match restart_session(restarter).await {
                Ok(_) => {
                    println!("Restarted the session.");
                    Ok(())
                }
                Err(err) => {
                    println!("Failed to restart session: {:?}", err);
                    Err(err)
                }
            }
        }
        Command::Add(AddCommand { element_url }) => {
            let element_manager = connect_to_service::<ElementManagerMarker>()?;
            match add_element(&element_url, element_manager).await {
                Ok(_) => {
                    println!("Added element: {:?}", element_url);
                    Ok(())
                }
                Err(err) => {
                    println!("Failed to add element: {:?}, {:?}", element_url, err);
                    Err(err)
                }
            }
        }
    }
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
        .launch(LaunchConfiguration {
            session_url: Some(session_url.to_string()),
            ..LaunchConfiguration::EMPTY
        })
        .await?;
    result.map_err(|err: fidl_fuchsia_session::LaunchError| format_err!("{:?}", err))?;
    Ok(())
}

/// Restarts a session.
///
/// # Parameters
/// - `restarter`: The Restarter proxy to use to restart the session.
///
/// # Errors
/// Returns an error if there is either an issue launching the session or
/// there isn't a previous session to be restarted.
async fn restart_session(restarter: RestarterProxy) -> Result<(), Error> {
    let result = restarter.restart().await?;
    result.map_err(|err: fidl_fuchsia_session::RestartError| format_err!("{:?}", err))?;
    Ok(())
}

/// Adds an element to the current session.
///
/// # Parameters
/// - `element_url`: The URL of the element to add.
/// - `element_manager`: The ElementManager to use when proposing the element.
///
/// # Errors
/// Returns an error if there is an issue adding the element.
async fn add_element(element_url: &str, element_manager: ElementManagerProxy) -> Result<(), Error> {
    let spec =
        ElementSpec { component_url: Some(element_url.to_string()), ..ElementSpec::new_empty() };
    let result = element_manager.propose_element(spec, None).await?;
    result.map_err(|err: fidl_fuchsia_session::ProposeElementError| format_err!("{:?}", err))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::{
            ElementManagerRequest, LaunchError, LauncherMarker, ProposeElementError, RestartError,
            RestarterMarker,
        },
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
                if let Some((configuration, responder)) = launch_request.into_launch() {
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
    /// is no LaunchConfiguration provided.
    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session_error() {
        let (launcher, mut launcher_server) =
            create_proxy_and_stream::<LauncherMarker>().expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        fasync::Task::spawn(async move {
            if let Some(launch_request) = launcher_server.try_next().await.unwrap() {
                if let Some((_, responder)) = launch_request.into_launch() {
                    let _ = responder.send(&mut Err(LaunchError::NotFound));
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
        let (restarter, mut restarter_server) =
            create_proxy_and_stream::<RestarterMarker>().expect("Failed to create Restarter FIDL.");

        fasync::Task::spawn(async move {
            if let Some(restarter_request) = restarter_server.try_next().await.unwrap() {
                if let Some(responder) = restarter_request.into_restart() {
                    let _ = responder.send(&mut Ok(()));
                } else {
                    assert!(false);
                }
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(restart_session(restarter).await.is_ok());
    }

    /// Tests that restart_session returns an error when an error is returned from fuchsia.session.Restarter.
    #[fasync::run_singlethreaded(test)]
    async fn test_restart_session_error() {
        let (restarter, mut restarter_server) =
            create_proxy_and_stream::<RestarterMarker>().expect("Failed to create Restarter FIDL.");

        fasync::Task::spawn(async move {
            if let Some(restarter_request) = restarter_server.try_next().await.unwrap() {
                if let Some(responder) = restarter_request.into_restart() {
                    let _ = responder.send(&mut Err(RestartError::NotFound));
                } else {
                    assert!(false);
                }
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(restart_session(restarter).await.is_err());
    }

    /// Tests that an element is added to the session
    #[fasync::run_singlethreaded(test)]
    async fn test_add_element() {
        let (proxy, mut server) = create_proxy_and_stream::<ElementManagerMarker>()
            .expect("Failed to create ElementManager FIDL.");
        let element_url = "test_element";

        fasync::Task::spawn(async move {
            if let Some(propose_request) = server.try_next().await.unwrap() {
                let ElementManagerRequest::ProposeElement { spec, responder, .. } = propose_request;
                assert_eq!(spec.component_url, Some(element_url.to_string()));
                let _ = responder.send(&mut Ok(()));
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(add_element(element_url, proxy).await.is_ok());
    }

    /// Tests that an element is added to the session
    #[fasync::run_singlethreaded(test)]
    async fn test_add_element_error() {
        let (proxy, mut server) = create_proxy_and_stream::<ElementManagerMarker>()
            .expect("Failed to create ElementManager FIDL.");
        let element_url = "test_element";

        fasync::Task::spawn(async move {
            if let Some(propose_request) = server.try_next().await.unwrap() {
                let ElementManagerRequest::ProposeElement { responder, .. } = propose_request;
                let _ = responder.send(&mut Err(ProposeElementError::Rejected));
            } else {
                assert!(false);
            }
        })
        .detach();

        assert!(add_element(element_url, proxy).await.is_err());
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
