// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::common_utils::fidl::connect_in_paths,
    crate::modular::types::{
        BasemgrResult, KillBasemgrResult, RestartSessionResult, StartBasemgrRequest,
    },
    anyhow::{format_err, Error},
    fidl::prelude::*,
    fidl_fuchsia_modular_internal as fmodular_internal, fidl_fuchsia_session as fsession,
    fuchsia_component::client::connect_to_protocol,
    lazy_static::lazy_static,
    serde_json::{from_value, Value},
    std::path::PathBuf,
};

lazy_static! {
    /// Path to the session component hub directory, used when basemgr is running as a v2 session.
    static ref SESSION_HUB_PATH: PathBuf =
        PathBuf::from("/hub-v2/children/core/children/session-manager/children/session:session");

    /// Path to basemgr's debug service exposed by a running session component.
    ///
    /// This is used to check if the session is both running and exposes the BasemgrDebug protocol.
    static ref BASEMGR_DEBUG_SESSION_EXEC_PATH: PathBuf = SESSION_HUB_PATH
        .join("exec/expose")
        .join(fmodular_internal::BasemgrDebugMarker::PROTOCOL_NAME);
}

enum BasemgrRuntimeState {
    // basemgr is running as a v2 session.
    V2Session,
}

/// Returns the state of the currently running basemgr instance, or None if not running.
fn get_basemgr_runtime_state() -> Option<BasemgrRuntimeState> {
    if BASEMGR_DEBUG_SESSION_EXEC_PATH.exists() {
        return Some(BasemgrRuntimeState::V2Session);
    }
    None
}

/// Returns a BasemgrDebugProxy served by the currently running basemgr (v1 or session),
/// or an Error if no session is running or the session does not expose this protocol.
fn connect_to_basemgr_debug() -> Result<fmodular_internal::BasemgrDebugProxy, Error> {
    connect_in_paths::<fmodular_internal::BasemgrDebugMarker>(&[BASEMGR_DEBUG_SESSION_EXEC_PATH
        .to_str()
        .unwrap()])?
    .ok_or_else(|| format_err!("Unable to connect to BasemgrDebug protocol"))
}

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct ModularFacade {
    session_launcher: fsession::LauncherProxy,
    session_restarter: fsession::RestarterProxy,
}

impl ModularFacade {
    pub fn new() -> ModularFacade {
        let session_launcher = connect_to_protocol::<fsession::LauncherMarker>()
            .expect("failed to connect to fuchsia.session.Launcher");
        let session_restarter = connect_to_protocol::<fsession::RestarterMarker>()
            .expect("failed to connect to fuchsia.session.Restarter");
        ModularFacade { session_launcher, session_restarter }
    }

    pub fn new_with_proxies(
        session_launcher: fsession::LauncherProxy,
        session_restarter: fsession::RestarterProxy,
    ) -> ModularFacade {
        ModularFacade { session_launcher, session_restarter }
    }

    /// Restarts the currently running session.
    pub async fn restart_session(&self) -> Result<RestartSessionResult, Error> {
        if self.session_restarter.restart().await?.is_err() {
            return Ok(RestartSessionResult::Fail);
        }
        Ok(RestartSessionResult::Success)
    }

    /// Facade to kill basemgr from Sl4f
    pub async fn kill_basemgr(&self) -> Result<KillBasemgrResult, Error> {
        if get_basemgr_runtime_state().is_none() {
            return Ok(KillBasemgrResult::NoBasemgrToKill);
        }
        connect_to_basemgr_debug()?.shutdown()?;
        Ok(KillBasemgrResult::Success)
    }

    /// Starts a Modular session, either as a v2 session or by launching basemgr
    /// as a legacy component.
    ///
    /// If basemgr is already running, it will be shut down first.
    ///
    /// `session_url` is required - basemgr will be started as a session with the given URL.
    ///
    /// # Arguments
    /// * `args`: A serde_json Value parsed into [`StartBasemgrRequest`]
    pub async fn start_basemgr(&self, args: Value) -> Result<BasemgrResult, Error> {
        let req: StartBasemgrRequest = from_value(args)?;

        // If basemgr is running, shut it down before starting a new one.
        if get_basemgr_runtime_state().is_some() {
            self.kill_basemgr().await?;
        }

        self.launch_session(&req.session_url).await?;

        Ok(BasemgrResult::Success)
    }

    /// Launches a session.
    ///
    /// # Arguments
    /// * `session_url`: Component URL for the session to launch.
    async fn launch_session(&self, session_url: &str) -> Result<(), Error> {
        let config = fsession::LaunchConfiguration {
            session_url: Some(session_url.to_string()),
            ..fsession::LaunchConfiguration::EMPTY
        };
        self.session_launcher
            .launch(config)
            .await?
            .map_err(|err| format_err!("failed to launch session: {:?}", err))
    }

    /// Facade that returns true if basemgr is running.
    pub fn is_basemgr_running(&self) -> Result<bool, Error> {
        Ok(get_basemgr_runtime_state().is_some())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::common_utils::namespace_binder::NamespaceBinder,
        assert_matches::assert_matches,
        fidl::endpoints::spawn_stream_handler,
        fidl_fuchsia_modular_internal as fmodular_internal,
        futures::channel::mpsc,
        futures::{SinkExt, StreamExt, TryStreamExt},
        lazy_static::lazy_static,
        serde_json::json,
        test_util::Counter,
        vfs::execution_scope::ExecutionScope,
    };

    #[fuchsia_async::run(2, test)]
    async fn test_start_basemgr_v2_without_config() -> Result<(), Error> {
        const TEST_SESSION_URL: &str =
            "fuchsia-pkg://fuchsia.com/test_session#meta/test_session.cm";

        lazy_static! {
            static ref SESSION_LAUNCH_CALL_COUNT: Counter = Counter::new(0);
        }

        let session_restarter = spawn_stream_handler(|_restarter_request| async {
            panic!("fuchsia.session.Restarter should not be called when starting");
        })?;

        let session_launcher = spawn_stream_handler(move |launcher_request| async move {
            match launcher_request {
                fsession::LauncherRequest::Launch { configuration, responder } => {
                    assert!(configuration.session_url.is_some());
                    let session_url = configuration.session_url.unwrap();
                    assert!(session_url == TEST_SESSION_URL.to_string());

                    SESSION_LAUNCH_CALL_COUNT.inc();
                    let _ = responder.send(&mut Ok(()));
                }
            }
        })?;

        let facade = ModularFacade::new_with_proxies(session_launcher, session_restarter);

        let start_basemgr_args = json!({
            "session_url": TEST_SESSION_URL,
        });
        assert_matches!(facade.start_basemgr(start_basemgr_args).await, Ok(BasemgrResult::Success));

        // The session should have been launched.
        assert_eq!(SESSION_LAUNCH_CALL_COUNT.get(), 1);

        Ok(())
    }

    #[fuchsia_async::run(2, test)]
    async fn test_start_basemgr_v2_shutdown_existing() -> Result<(), Error> {
        const TEST_SESSION_URL: &str =
            "fuchsia-pkg://fuchsia.com/test_session#meta/test_session.cm";

        lazy_static! {
            static ref SESSION_LAUNCH_CALL_COUNT: Counter = Counter::new(0);
        }

        let session_restarter = spawn_stream_handler(|_restarter_request| async {
            panic!("fuchsia.session.Restarter should not be called when starting");
        })?;

        let session_launcher = spawn_stream_handler(move |launcher_request| async move {
            match launcher_request {
                fsession::LauncherRequest::Launch { configuration, responder } => {
                    assert!(configuration.session_url.is_some());
                    let session_url = configuration.session_url.unwrap();
                    assert!(session_url == TEST_SESSION_URL.to_string());

                    SESSION_LAUNCH_CALL_COUNT.inc();
                    let _ = responder.send(&mut Ok(()));
                }
            }
        })?;

        let facade = ModularFacade::new_with_proxies(session_launcher, session_restarter);

        let scope = ExecutionScope::new();
        let mut ns = NamespaceBinder::new(scope);

        let (called_shutdown_tx, mut called_shutdown_rx) = mpsc::channel(0);

        // Add an entry for the BasemgrDebug protocol into the hub under the session path
        // so that `start_basemgr` knows there is a session running.
        let basemgr_debug =
            vfs::service::host(move |mut stream: fmodular_internal::BasemgrDebugRequestStream| {
                let mut called_shutdown_tx = called_shutdown_tx.clone();
                async move {
                    while let Ok(Some(request)) = stream.try_next().await {
                        match request {
                            fmodular_internal::BasemgrDebugRequest::Shutdown { .. } => {
                                called_shutdown_tx
                                    .send(())
                                    .await
                                    .expect("could not send on channel");
                            }
                            _ => {
                                panic!(
                                    "BasemgrDebug methods other than Shutdown should not be called"
                                );
                            }
                        }
                    }
                }
            });
        ns.bind_at_path(BASEMGR_DEBUG_SESSION_EXEC_PATH.to_str().unwrap(), basemgr_debug)?;

        let start_basemgr_args = json!({
            "session_url": TEST_SESSION_URL,
        });
        assert_matches!(facade.start_basemgr(start_basemgr_args).await, Ok(BasemgrResult::Success));

        // The existing session should have been shut down.
        assert_eq!(called_shutdown_rx.next().await, Some(()));

        // The session should have been launched.
        assert_eq!(SESSION_LAUNCH_CALL_COUNT.get(), 1);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_is_basemgr_running_not_running() -> Result<(), Error> {
        // This test does not bind the protocols that is_basemgr_running expects to be
        // in the namespace for either a session or legacy basemgr component to
        // simulate the case when neither is running.

        let session_launcher = spawn_stream_handler(|_launcher_request| async {
            panic!("ModularFacade.is_basemgr_running should not use fuchsia.session.Launcher");
        })?;

        let session_restarter = spawn_stream_handler(|_restarter_request| async {
            panic!("ModularFacade.is_basemgr_running should not use fuchsia.session.Restarter");
        })?;

        let facade = ModularFacade::new_with_proxies(session_launcher, session_restarter);

        assert_matches!(facade.is_basemgr_running(), Ok(false));

        Ok(())
    }

    #[fuchsia_async::run(2, test)]
    async fn test_is_basemgr_running_v2() -> Result<(), Error> {
        let scope = ExecutionScope::new();
        let mut ns = NamespaceBinder::new(scope);

        // Serve the `fuchsia.modular.internal.BasemgrDebug` protocol in the hub path
        // for the session. This simulates a running session.
        ns.bind_at_path(
            BASEMGR_DEBUG_SESSION_EXEC_PATH.to_str().unwrap(),
            vfs::service::host(|_stream: fmodular_internal::BasemgrDebugRequestStream| async {
                panic!("ModularFacade.is_basemgr_running should not connect to BasemgrDebug");
            }),
        )?;

        let session_launcher = spawn_stream_handler(|_launcher_request| async {
            panic!("ModularFacade.is_basemgr_running should not use fuchsia.session.Launcher");
        })?;

        let session_restarter = spawn_stream_handler(|_restarter_request| async {
            panic!("ModularFacade.is_basemgr_running should not use fuchsia.session.Restarter");
        })?;

        let facade = ModularFacade::new_with_proxies(session_launcher, session_restarter);

        assert_matches!(facade.is_basemgr_running(), Ok(true));

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_restart_session() -> Result<(), Error> {
        lazy_static! {
            static ref RESTART_CALL_COUNT: Counter = Counter::new(0);
        }

        let session_launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("fuchsia.session.Launcher should not be called when restarting");
        })?;

        let session_restarter = spawn_stream_handler(move |restarter_request| async move {
            match restarter_request {
                fsession::RestarterRequest::Restart { responder } => {
                    RESTART_CALL_COUNT.inc();
                    let _ = responder.send(&mut Ok(()));
                }
            }
        })?;

        let facade = ModularFacade::new_with_proxies(session_launcher, session_restarter);

        assert_matches!(facade.restart_session().await, Ok(RestartSessionResult::Success));
        assert_eq!(RESTART_CALL_COUNT.get(), 1);

        Ok(())
    }
}
