// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_fuchsia_element as felement, fidl_fuchsia_session as fsession, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_protocol_at_dir_root, connect_to_protocol_at_path},
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

/// Moniker of session-manager component
const SESSION_MANAGER_MONIKER: &str = "./core/session-manager";

// Moniker of session component
const SESSION_MONIKER: &str = "./core/session-manager/session:session";

async fn connect_to_exposed_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
    realm_query: &fsys::RealmQueryProxy,
    moniker: &str,
) -> Result<P::Proxy, Error> {
    let resolved_dirs = realm_query
        .get_instance_directories(moniker)
        .await?
        .map_err(|e| format_err!("RealmQuery error: {:?}", e))?
        .ok_or(format_err!("{} is not resolved", moniker))?;
    let exposed_dir = resolved_dirs.exposed_dir.into_proxy()?;
    connect_to_protocol_at_dir_root::<P>(&exposed_dir)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Args { command } = argh::from_env();
    let realm_query =
        connect_to_protocol_at_path::<fsys::RealmQueryMarker>("/svc/fuchsia.sys2.RealmQuery.root")
            .unwrap();

    match command {
        Command::Launch(LaunchCommand { session_url }) => {
            let launcher = connect_to_exposed_protocol::<fsession::LauncherMarker>(
                &realm_query,
                SESSION_MANAGER_MONIKER,
            )
            .await?;
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
            let restarter = connect_to_exposed_protocol::<fsession::RestarterMarker>(
                &realm_query,
                SESSION_MANAGER_MONIKER,
            )
            .await?;
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
            let manager_proxy = connect_to_exposed_protocol::<felement::ManagerMarker>(
                &realm_query,
                SESSION_MONIKER,
            )
            .await?;
            match add_element(&element_url, manager_proxy).await {
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
async fn launch_session(session_url: &str, launcher: fsession::LauncherProxy) -> Result<(), Error> {
    let result = launcher
        .launch(fsession::LaunchConfiguration {
            session_url: Some(session_url.to_string()),
            ..fsession::LaunchConfiguration::EMPTY
        })
        .await?;
    result.map_err(|err: fsession::LaunchError| format_err!("{:?}", err))?;
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
async fn restart_session(restarter: fsession::RestarterProxy) -> Result<(), Error> {
    let result = restarter.restart().await?;
    result.map_err(|err: fsession::RestartError| format_err!("{:?}", err))?;
    Ok(())
}

/// Adds an element to the current session.
///
/// # Parameters
/// - `element_url`: The URL of the element to add.
/// - `manager_proxy`: The element manager proxy use to propose the element.
///
/// # Errors
/// Returns an error if there is an issue adding the element.
async fn add_element(
    element_url: &str,
    manager_proxy: felement::ManagerProxy,
) -> Result<(), Error> {
    let spec =
        felement::Spec { component_url: Some(element_url.to_string()), ..felement::Spec::EMPTY };
    manager_proxy
        .propose_element(spec, None)
        .await?
        .map_err(|err: felement::ProposeElementError| format_err!("{:?}", err))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        // fidl_fuchsia.sys2::EventType, TODO(fxbug.dev/47730): re-enable the tests.
        fuchsia_async as fasync,
        futures::TryStreamExt,
        // test_utils_lib::events::{EventSource, Ordering, RecordedEvent},
    };

    /// Tests that the session_control tool successfully handles a call to LaunchSession with the provided URL.
    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session() {
        let (launcher, mut launcher_server) = create_proxy_and_stream::<fsession::LauncherMarker>()
            .expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        let _server_task = fasync::Task::spawn(async move {
            let launch_request =
                launcher_server.try_next().await.expect("FIDL Error").expect("Stream terminated");
            let (configuration, responder) =
                launch_request.into_launch().expect("Failed to unwrap launch request");
            assert_eq!(configuration.session_url, Some(session_url.to_string()));
            let _ = responder.send(&mut Ok(()));
        });

        assert!(launch_session(&session_url, launcher).await.is_ok());
    }

    /// Tests that the session_control tool returns an error on a call to LaunchSession when there
    /// is no LaunchConfiguration provided.
    #[fasync::run_singlethreaded(test)]
    async fn test_launch_session_error() {
        let (launcher, mut launcher_server) = create_proxy_and_stream::<fsession::LauncherMarker>()
            .expect("Failed to create Launcher FIDL.");
        let session_url = "test_session";

        let _server_task = fasync::Task::spawn(async move {
            let launch_request =
                launcher_server.try_next().await.expect("FIDL Error").expect("Stream terminated");
            let (_, responder) =
                launch_request.into_launch().expect("Failed to unwrap launch request");
            let _ = responder.send(&mut Err(fsession::LaunchError::NotFound));
        });

        assert!(launch_session(&session_url, launcher).await.is_err());
    }

    /// Tests that restart_session makes the appropriate calls to fuchsia.session.Launcher.
    #[fasync::run_singlethreaded(test)]
    async fn test_restart_session() {
        let (restarter, mut restarter_server) =
            create_proxy_and_stream::<fsession::RestarterMarker>()
                .expect("Failed to create Restarter FIDL.");

        let _server_task = fasync::Task::spawn(async move {
            let restarter_request =
                restarter_server.try_next().await.expect("FIDL Error").expect("Stream terminated");
            let responder =
                restarter_request.into_restart().expect("Failed to unwrap restarter request");
            let _ = responder.send(&mut Ok(()));
        });

        assert!(restart_session(restarter).await.is_ok());
    }

    /// Tests that restart_session returns an error when an error is returned from fuchsia.session.Restarter.
    #[fasync::run_singlethreaded(test)]
    async fn test_restart_session_error() {
        let (restarter, mut restarter_server) =
            create_proxy_and_stream::<fsession::RestarterMarker>()
                .expect("Failed to create Restarter FIDL.");

        let _server_task = fasync::Task::spawn(async move {
            let restarter_request =
                restarter_server.try_next().await.expect("FIDL Error").expect("Stream terminated");
            let responder =
                restarter_request.into_restart().expect("Failed to unwrap restarter request");
            let _ = responder.send(&mut Err(fsession::RestartError::NotFound));
        });

        assert!(restart_session(restarter).await.is_err());
    }

    /// Tests that an element is added to the session
    #[fasync::run_singlethreaded(test)]
    async fn test_add_element() {
        let (manager, mut manager_server) = create_proxy_and_stream::<felement::ManagerMarker>()
            .expect("Failed to create Manager FIDL.");
        let element_url = "test_element";

        let _server_task = fasync::Task::spawn(async move {
            let propose_request =
                manager_server.try_next().await.expect("FIDL Error").expect("Stream terminated");
            let felement::ManagerRequest::ProposeElement { spec, responder, .. } = propose_request;
            assert_eq!(spec.component_url, Some(element_url.to_string()));
            let _ = responder.send(&mut Ok(()));
        });

        assert!(add_element(element_url, manager).await.is_ok());
    }

    /// Tests that an element is added to the session
    #[fasync::run_singlethreaded(test)]
    async fn test_add_element_error() {
        let (manager, mut manager_server) = create_proxy_and_stream::<felement::ManagerMarker>()
            .expect("Failed to create Manager FIDL.");
        let element_url = "test_element";

        let _server_task = fasync::Task::spawn(async move {
            let propose_request =
                manager_server.try_next().await.expect("FIDL Error").expect("Stream terminated");
            let felement::ManagerRequest::ProposeElement { responder, .. } = propose_request;
            let _ = responder.send(&mut Err(felement::ProposeElementError::InvalidArgs));
        });

        assert!(add_element(element_url, manager).await.is_err());
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
