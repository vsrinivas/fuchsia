// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::startup,
    anyhow::{Context as _, Error},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_element as felement,
    fidl_fuchsia_session::{
        LaunchConfiguration, LaunchError, LauncherRequest, LauncherRequestStream, RestartError,
        RestarterRequest, RestarterRequestStream,
    },
    fidl_fuchsia_sessionmanager::StartupRequestStream,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt, TryFutureExt, TryStreamExt},
    std::sync::Arc,
    tracing::{error, info},
};

/// Maximum number of concurrent connections to the protocols served by SessionManager.
const MAX_CONCURRENT_CONNECTIONS: usize = 10_000;

/// A request to connect to a protocol exposed by SessionManager.
enum IncomingRequest {
    Manager(felement::ManagerRequestStream),
    GraphicalPresenter(felement::GraphicalPresenterRequestStream),
    Launcher(LauncherRequestStream),
    Restarter(RestarterRequestStream),
    Startup(StartupRequestStream),
}

struct SessionManagerState {
    /// The URL of the most recently launched session.
    ///
    /// If set, the session is not guaranteed to be running.
    session_url: Option<String>,

    /// A client-end channel to the most recently launched session's `exposed_dir`.
    ///
    /// If set, the session is not guaranteed to be running, and the channel is not
    /// guaranteed to be connected.
    session_exposed_dir_channel: Option<zx::Channel>,

    /// The realm in which sessions will be launched.
    realm: fcomponent::RealmProxy,
}

/// Manages the session lifecycle and provides services to control the session.
#[derive(Clone)]
pub struct SessionManager {
    state: Arc<Mutex<SessionManagerState>>,
}

impl SessionManager {
    /// Constructs a new SessionManager.
    ///
    /// # Parameters
    /// - `realm`: The realm in which sessions will be launched.
    pub fn new(realm: fcomponent::RealmProxy) -> Self {
        let state =
            SessionManagerState { session_url: None, session_exposed_dir_channel: None, realm };
        SessionManager { state: Arc::new(Mutex::new(state)) }
    }

    /// Launch the session with the component URL in `session_url`.
    ///
    /// # Errors
    ///
    /// Returns an error if the session could not be launched.
    pub async fn launch_startup_session(&mut self, session_url: String) -> Result<(), Error> {
        let mut state = self.state.lock().await;
        state.session_exposed_dir_channel =
            Some(startup::launch_session(&session_url, &state.realm).await?);
        state.session_url = Some(session_url);
        Ok(())
    }

    /// Starts serving [`IncomingRequest`] from `svc`.
    ///
    /// This will return once the [`ServiceFs`] stops serving requests.
    ///
    /// # Errors
    /// Returns an error if there is an issue serving the `svc` directory handle.
    pub async fn serve(&mut self) -> Result<(), Error> {
        let mut fs = ServiceFs::new_local();
        fs.dir("svc")
            .add_fidl_service(IncomingRequest::Manager)
            .add_fidl_service(IncomingRequest::GraphicalPresenter)
            .add_fidl_service(IncomingRequest::Launcher)
            .add_fidl_service(IncomingRequest::Restarter)
            .add_fidl_service(IncomingRequest::Startup);
        fs.take_and_serve_directory_handle()?;

        fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, |request| {
            let mut session_manager = self.clone();
            async move {
                session_manager
                    .handle_incoming_request(request)
                    .unwrap_or_else(|err| error!(?err))
                    .await
            }
        })
        .await;

        Ok(())
    }

    /// Handles an [`IncomingRequest`].
    ///
    /// This will return once the protocol connection has been closed.
    ///
    /// # Errors
    /// Returns an error if there is an issue serving the request.
    async fn handle_incoming_request(&mut self, request: IncomingRequest) -> Result<(), Error> {
        match request {
            IncomingRequest::Manager(request_stream) => {
                // Connect to element.Manager served by the session.
                let (manager_proxy, server_end) =
                    fidl::endpoints::create_proxy::<felement::ManagerMarker>()
                        .context("Failed to create ManagerProxy")?;
                {
                    let state = self.state.lock().await;
                    let session_exposed_dir_channel =
                        state.session_exposed_dir_channel.as_ref().context(
                            "Failed to connect to ManagerProxy because no session was started",
                        )?;
                    fdio::service_connect_at(
                        session_exposed_dir_channel,
                        "fuchsia.element.Manager",
                        server_end.into_channel(),
                    )
                    .context("Failed to connect to Manager service")?;
                }
                SessionManager::handle_manager_request_stream(request_stream, manager_proxy)
                    .await
                    .context("Manager request stream got an error.")?;
            }
            IncomingRequest::GraphicalPresenter(request_stream) => {
                // Connect to GraphicalPresenter served by the session.
                let (graphical_presenter_proxy, server_end) =
                    fidl::endpoints::create_proxy::<felement::GraphicalPresenterMarker>()
                        .context("Failed to create GraphicalPresenterProxy")?;
                {
                    let state = self.state.lock().await;
                    let session_exposed_dir_channel = state
                        .session_exposed_dir_channel
                        .as_ref()
                        .context(
                        "Failed to connect to GraphicalPresenterProxy because no session was started",
                    )?;
                    fdio::service_connect_at(
                        session_exposed_dir_channel,
                        "fuchsia.element.GraphicalPresenter",
                        server_end.into_channel(),
                    )
                    .context("Failed to connect to GraphicalPresenter service")?;
                }
                SessionManager::handle_graphical_presenter_request_stream(
                    request_stream,
                    graphical_presenter_proxy,
                )
                .await
                .context("Graphical Presenter request stream got an error.")?;
            }
            IncomingRequest::Launcher(request_stream) => {
                self.handle_launcher_request_stream(request_stream)
                    .await
                    .context("Session Launcher request stream got an error.")?;
            }
            IncomingRequest::Restarter(request_stream) => {
                self.handle_restarter_request_stream(request_stream)
                    .await
                    .context("Session Restarter request stream got an error.")?;
            }
            IncomingRequest::Startup(request_stream) => {
                self.handle_startup_request_stream(request_stream)
                    .await
                    .context("Sessionmanager Startup request stream got an error.")?;
            }
        }

        Ok(())
    }

    /// Serves a specified [`ManagerRequestStream`].
    ///
    /// # Parameters
    /// - `request_stream`: the ManagerRequestStream.
    /// - `manager_proxy`: the ManagerProxy that will handle the relayed commands.
    ///
    /// # Errors
    /// When an error is encountered reading from the request stream.
    pub async fn handle_manager_request_stream(
        mut request_stream: felement::ManagerRequestStream,
        manager_proxy: felement::ManagerProxy,
    ) -> Result<(), Error> {
        while let Some(request) =
            request_stream.try_next().await.context("Error handling Manager request stream")?
        {
            match request {
                felement::ManagerRequest::ProposeElement { spec, controller, responder } => {
                    let mut result = manager_proxy.propose_element(spec, controller).await?;
                    responder.send(&mut result)?;
                }
            };
        }
        Ok(())
    }

    /// Serves a specified [`GraphicalPresenterRequestStream`].
    ///
    /// # Parameters
    /// - `request_stream`: the GraphicalPresenterRequestStream.
    /// - `graphical_presenter_proxy`: the GraphicalPresenterProxy that will handle the relayed commands.
    ///
    /// # Errors
    /// When an error is encountered reading from the request stream.
    pub async fn handle_graphical_presenter_request_stream(
        mut request_stream: felement::GraphicalPresenterRequestStream,
        graphical_presenter_proxy: felement::GraphicalPresenterProxy,
    ) -> Result<(), Error> {
        while let Some(request) = request_stream
            .try_next()
            .await
            .context("Error handling Graphical Presenter request stream")?
        {
            match request {
                felement::GraphicalPresenterRequest::PresentView {
                    view_spec,
                    annotation_controller,
                    view_controller_request,
                    responder,
                } => {
                    let mut result = graphical_presenter_proxy
                        .present_view(view_spec, annotation_controller, view_controller_request)
                        .await?;
                    responder.send(&mut result)?;
                }
            };
        }
        Ok(())
    }

    /// Serves a specified [`LauncherRequestStream`].
    ///
    /// # Parameters
    /// - `request_stream`: the LauncherRequestStream.
    ///
    /// # Errors
    /// When an error is encountered reading from the request stream.
    pub async fn handle_launcher_request_stream(
        &mut self,
        mut request_stream: LauncherRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            request_stream.try_next().await.context("Error handling Launcher request stream")?
        {
            match request {
                LauncherRequest::Launch { configuration, responder } => {
                    let mut result = self.handle_launch_request(configuration).await;
                    let _ = responder.send(&mut result);
                }
            };
        }
        Ok(())
    }

    pub async fn handle_startup_request_stream(
        &mut self,
        mut request_stream: StartupRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            request_stream.try_next().await.context("Error handling Startup request stream")?
        {
            match request {
                _ => {
                    // No-op
                    info!("Received startup request.");
                }
            };
        }
        Ok(())
    }

    /// Serves a specified [`RestarterRequestStream`].
    ///
    /// # Parameters
    /// - `request_stream`: the RestarterRequestStream.
    ///
    /// # Errors
    /// When an error is encountered reading from the request stream.
    pub async fn handle_restarter_request_stream(
        &mut self,
        mut request_stream: RestarterRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            request_stream.try_next().await.context("Error handling Restarter request stream")?
        {
            match request {
                RestarterRequest::Restart { responder } => {
                    let mut result = self.handle_restart_request().await;
                    let _ = responder.send(&mut result);
                }
            };
        }
        Ok(())
    }

    /// Handles calls to Launcher.Launch().
    ///
    /// # Parameters
    /// - configuration: The launch configuration for the new session.
    async fn handle_launch_request(
        &mut self,
        configuration: LaunchConfiguration,
    ) -> Result<(), LaunchError> {
        if let Some(session_url) = configuration.session_url {
            let mut state = self.state.lock().await;
            startup::launch_session(&session_url, &state.realm)
                .await
                .map_err(|err| match err {
                    startup::StartupError::NotDestroyed { .. } => {
                        LaunchError::DestroyComponentFailed
                    }
                    startup::StartupError::NotCreated { err, .. } => match err {
                        fcomponent::Error::InstanceCannotResolve => LaunchError::NotFound,
                        _ => LaunchError::CreateComponentFailed,
                    },
                    startup::StartupError::ExposedDirNotOpened { .. } => {
                        LaunchError::CreateComponentFailed
                    }
                    startup::StartupError::NotLaunched { .. } => LaunchError::CreateComponentFailed,
                })
                .map(|session_exposed_dir_channel| {
                    state.session_url = Some(session_url);
                    state.session_exposed_dir_channel = Some(session_exposed_dir_channel);
                })
        } else {
            Err(LaunchError::NotFound)
        }
    }

    /// Handles calls to Restarter.Restart().
    async fn handle_restart_request(&mut self) -> Result<(), RestartError> {
        let mut state = self.state.lock().await;
        if let Some(ref session_url) = state.session_url {
            startup::launch_session(&session_url, &state.realm)
                .await
                .map_err(|err| match err {
                    startup::StartupError::NotDestroyed { .. } => {
                        RestartError::DestroyComponentFailed
                    }
                    startup::StartupError::NotCreated { err, .. } => match err {
                        fcomponent::Error::InstanceCannotResolve => RestartError::NotFound,
                        _ => RestartError::CreateComponentFailed,
                    },
                    startup::StartupError::ExposedDirNotOpened { .. } => {
                        RestartError::CreateComponentFailed
                    }
                    startup::StartupError::NotLaunched { .. } => {
                        RestartError::CreateComponentFailed
                    }
                })
                .map(|session_exposed_dir_channel| {
                    state.session_exposed_dir_channel = Some(session_exposed_dir_channel);
                })
        } else {
            Err(RestartError::NotRunning)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::SessionManager,
        fidl::endpoints::{create_proxy_and_stream, spawn_stream_handler},
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_element as felement,
        fidl_fuchsia_session::{
            LaunchConfiguration, LauncherMarker, LauncherProxy, RestartError, RestarterMarker,
            RestarterProxy,
        },
        futures::prelude::*,
        session_testing::spawn_noop_directory_server,
    };

    fn serve_session_manager_services(
        session_manager: SessionManager,
    ) -> (LauncherProxy, RestarterProxy) {
        let (launcher_proxy, launcher_stream) =
            create_proxy_and_stream::<LauncherMarker>().unwrap();
        {
            let mut session_manager_ = session_manager.clone();
            fuchsia_async::Task::spawn(async move {
                session_manager_
                    .handle_launcher_request_stream(launcher_stream)
                    .await
                    .expect("Session launcher request stream got an error.");
            })
            .detach();
        }

        let (restarter_proxy, restarter_stream) =
            create_proxy_and_stream::<RestarterMarker>().unwrap();
        {
            let mut session_manager_ = session_manager.clone();
            fuchsia_async::Task::spawn(async move {
                session_manager_
                    .handle_restarter_request_stream(restarter_stream)
                    .await
                    .expect("Session restarter request stream got an error.");
            })
            .detach();
        }

        (launcher_proxy, restarter_proxy)
    }

    /// Verifies that Launcher.Launch creates a new session.
    #[fuchsia::test]
    async fn test_launch() {
        let session_url = "session";

        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fcomponent::RealmRequest::DestroyChild { child: _, responder } => {
                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::CreateChild {
                    collection: _,
                    decl,
                    args: _,
                    responder,
                } => {
                    assert_eq!(decl.url.unwrap(), session_url);
                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::OpenExposedDir { child: _, exposed_dir, responder } => {
                    spawn_noop_directory_server(exposed_dir);
                    let _ = responder.send(&mut Ok(()));
                }
                _ => panic!("Realm handler received an unexpected request"),
            };
        })
        .unwrap();

        let session_manager = SessionManager::new(realm);
        let (launcher, _restarter) = serve_session_manager_services(session_manager);

        assert!(launcher
            .launch(LaunchConfiguration {
                session_url: Some(session_url.to_string()),
                ..LaunchConfiguration::EMPTY
            })
            .await
            .is_ok());
    }

    /// Verifies that Launcher.Restart restarts an existing session.
    #[fuchsia::test]
    async fn test_restart() {
        let session_url = "session";

        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fcomponent::RealmRequest::DestroyChild { child: _, responder } => {
                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::CreateChild {
                    collection: _,
                    decl,
                    args: _,
                    responder,
                } => {
                    assert_eq!(decl.url.unwrap(), session_url);
                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::OpenExposedDir { child: _, exposed_dir, responder } => {
                    spawn_noop_directory_server(exposed_dir);
                    let _ = responder.send(&mut Ok(()));
                }
                _ => panic!("Realm handler received an unexpected request"),
            };
        })
        .unwrap();

        let session_manager = SessionManager::new(realm);
        let (launcher, restarter) = serve_session_manager_services(session_manager);

        assert!(launcher
            .launch(LaunchConfiguration {
                session_url: Some(session_url.to_string()),
                ..LaunchConfiguration::EMPTY
            })
            .await
            .expect("could not call Launch")
            .is_ok());

        assert!(restarter.restart().await.expect("could not call Restart").is_ok());
    }

    /// Verifies that Launcher.Restart return an error if there is no running existing session.
    #[fuchsia::test]
    async fn test_restart_error_not_running() {
        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests as there is no session to launch")
        })
        .unwrap();

        let session_manager = SessionManager::new(realm);
        let (_launcher, restarter) = serve_session_manager_services(session_manager);

        assert_eq!(
            Err(RestartError::NotRunning),
            restarter.restart().await.expect("could not call Restart")
        );
    }

    #[fuchsia::test]
    async fn handle_element_manager_request_stream_propagates_request_to_downstream_service() {
        let (local_proxy, local_request_stream) =
            create_proxy_and_stream::<felement::ManagerMarker>()
                .expect("Failed to create local Manager proxy and stream");

        let (downstream_proxy, mut downstream_request_stream) =
            create_proxy_and_stream::<felement::ManagerMarker>()
                .expect("Failed to create downstream Manager proxy and stream");

        let element_url = "element_url";
        let mut num_elements_proposed = 0;

        let local_server_fut =
            SessionManager::handle_manager_request_stream(local_request_stream, downstream_proxy);

        let downstream_server_fut = async {
            while let Some(request) = downstream_request_stream.try_next().await.unwrap() {
                match request {
                    felement::ManagerRequest::ProposeElement { spec, responder, .. } => {
                        num_elements_proposed += 1;
                        assert_eq!(Some(element_url.to_string()), spec.component_url);
                        let _ = responder.send(&mut Ok(()));
                    }
                }
            }
        };

        let propose_and_drop_fut = async {
            local_proxy
                .propose_element(
                    felement::Spec {
                        component_url: Some(element_url.to_string()),
                        ..felement::Spec::EMPTY
                    },
                    None,
                )
                .await
                .expect("Failed to call ProposeElement")
                .expect("Failed to propose element");

            std::mem::drop(local_proxy); // Drop proxy to terminate `server_fut`.
        };

        let _ = future::join3(propose_and_drop_fut, local_server_fut, downstream_server_fut).await;

        assert_eq!(num_elements_proposed, 1);
    }
}
