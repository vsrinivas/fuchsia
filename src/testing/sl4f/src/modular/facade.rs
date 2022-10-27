// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::modular::types::{
        BasemgrResult, KillBasemgrResult, RestartSessionResult, StartBasemgrRequest,
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_session as fsession,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol,
    serde_json::{from_value, Value},
};

// The session runs as `./core/session-manager/session:session`. The parts are:
const SESSION_PARENT_MONIKER: &str = "./core/session-manager";
const SESSION_COLLECTION_NAME: &str = "session";
const SESSION_CHILD_NAME: &str = "session";
const SESSION_MONIKER: &str = "./core/session-manager/session:session";

enum BasemgrRuntimeState {
    // basemgr is running as a v2 session.
    V2Session,
}

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct ModularFacade {
    session_launcher: fsession::LauncherProxy,
    session_restarter: fsession::RestarterProxy,
    lifecycle_controller: fsys::LifecycleControllerProxy,
    realm_query: fsys::RealmQueryProxy,
}

impl ModularFacade {
    pub fn new() -> ModularFacade {
        let session_launcher = connect_to_protocol::<fsession::LauncherMarker>()
            .expect("failed to connect to fuchsia.session.Launcher");
        let session_restarter = connect_to_protocol::<fsession::RestarterMarker>()
            .expect("failed to connect to fuchsia.session.Restarter");
        let lifecycle_controller = connect_to_protocol::<fsys::LifecycleControllerMarker>()
            .expect("failed to connect to fuchsia.sys2.LifecycleController");
        let realm_query =
            fuchsia_component::client::connect_to_protocol::<fsys::RealmQueryMarker>()
                .expect("failed to connect to fuchsia.sys2.RealmQuery");
        ModularFacade { session_launcher, session_restarter, lifecycle_controller, realm_query }
    }

    pub fn new_with_proxies(
        session_launcher: fsession::LauncherProxy,
        session_restarter: fsession::RestarterProxy,
        lifecycle_controller: fsys::LifecycleControllerProxy,
        realm_query: fsys::RealmQueryProxy,
    ) -> ModularFacade {
        ModularFacade { session_launcher, session_restarter, lifecycle_controller, realm_query }
    }

    /// Returns the state of the currently running basemgr instance, or None if not running.
    async fn get_basemgr_runtime_state(&self) -> Option<BasemgrRuntimeState> {
        let result = if let Ok((info, _)) =
            self.realm_query.get_instance_info(SESSION_MONIKER).await.ok()?
        {
            if info.state == fsys::InstanceState::Started {
                Some(BasemgrRuntimeState::V2Session)
            } else {
                None
            }
        } else {
            None
        };
        result
    }

    /// Restarts the currently running session.
    pub async fn restart_session(&self) -> Result<RestartSessionResult, Error> {
        if self.session_restarter.restart().await?.is_err() {
            return Ok(RestartSessionResult::Fail);
        }
        Ok(RestartSessionResult::Success)
    }

    /// Facade to kill basemgr from Sl4f.
    pub async fn kill_basemgr(&self) -> Result<KillBasemgrResult, Error> {
        if self.get_basemgr_runtime_state().await.is_none() {
            return Ok(KillBasemgrResult::NoBasemgrToKill);
        }

        // Use a root LifecycleController to kill the session. It will send a shutdown signal to the
        // session so it can terminate gracefully.
        self.lifecycle_controller
            .destroy_child(
                SESSION_PARENT_MONIKER,
                &mut fdecl::ChildRef {
                    name: SESSION_CHILD_NAME.to_string(),
                    collection: Some(SESSION_COLLECTION_NAME.to_string()),
                },
            )
            .await?
            .map_err(|err| format_err!("failed to destroy session: {:?}", err))?;

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
        if self.get_basemgr_runtime_state().await.is_some() {
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
    pub async fn is_basemgr_running(&self) -> Result<bool, Error> {
        Ok(self.get_basemgr_runtime_state().await.is_some())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::modular::{
            facade::{
                ModularFacade, SESSION_CHILD_NAME, SESSION_COLLECTION_NAME, SESSION_MONIKER,
                SESSION_PARENT_MONIKER,
            },
            types::{BasemgrResult, KillBasemgrResult, RestartSessionResult},
        },
        anyhow::Error,
        assert_matches::assert_matches,
        fidl::endpoints::{create_endpoints, spawn_stream_handler},
        fidl_fuchsia_io as fio, fidl_fuchsia_session as fsession, fidl_fuchsia_sys2 as fsys,
        futures::Future,
        lazy_static::lazy_static,
        serde_json::json,
        test_util::Counter,
    };

    const TEST_SESSION_URL: &str = "fuchsia-pkg://fuchsia.com/test_session#meta/test_session.cm";

    // This function sets up a facade with launcher, restarter, and lifecycle_controller mocks and
    // provides counters to check how often each is called. To use it, pass in a function that
    // operates on a facade, executing the operations of the test, and pass in the expected number
    // of calls to launch, destroy, and restart.
    async fn test_facade<Fut>(
        run_facade_fns: impl Fn(ModularFacade) -> Fut,
        is_basemgr_running: bool,
        expected_launch_count: usize,
        expected_destroy_count: usize,
        expected_restart_count: usize,
    ) -> Result<(), Error>
    where
        Fut: Future<Output = Result<(), Error>>,
    {
        lazy_static! {
            static ref SESSION_LAUNCH_CALL_COUNT: Counter = Counter::new(0);
            static ref DESTROY_CHILD_CALL_COUNT: Counter = Counter::new(0);
            static ref SESSION_RESTART_CALL_COUNT: Counter = Counter::new(0);
        }

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

        let session_restarter = spawn_stream_handler(move |restarter_request| async move {
            match restarter_request {
                fsession::RestarterRequest::Restart { responder } => {
                    SESSION_RESTART_CALL_COUNT.inc();
                    let _ = responder.send(&mut Ok(()));
                }
            }
        })?;

        let lifecycle_controller = spawn_stream_handler(|lifecycle_controller_request| async {
            match lifecycle_controller_request {
                fsys::LifecycleControllerRequest::DestroyChild {
                    parent_moniker,
                    child,
                    responder,
                } => {
                    assert_eq!(parent_moniker, SESSION_PARENT_MONIKER.to_string());
                    assert_eq!(child.name, SESSION_CHILD_NAME);
                    assert_eq!(child.collection.unwrap(), SESSION_COLLECTION_NAME);

                    DESTROY_CHILD_CALL_COUNT.inc();
                    let _ = responder.send(&mut Ok(()));
                }
                r => {
                    panic!("didn't expect request: {:?}", r)
                }
            }
        })?;

        let realm_query = spawn_stream_handler(move |realm_query_request| async move {
            let (exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
            let (ns_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

            match realm_query_request {
                fsys::RealmQueryRequest::GetInstanceInfo { moniker, responder } => {
                    assert_eq!(moniker, SESSION_MONIKER.to_string());
                    let mut result = if is_basemgr_running {
                        let info = fsys::InstanceInfo {
                            moniker: SESSION_MONIKER.to_string(),
                            url: "fake".to_string(),
                            instance_id: None,
                            state: fsys::InstanceState::Started,
                        };
                        let resolved = Some(Box::new(fsys::ResolvedState {
                            uses: vec![],
                            exposes: vec![],
                            config: None,
                            pkg_dir: None,
                            exposed_dir,
                            ns_dir,
                            execution: None,
                        }));
                        Ok((info, resolved))
                    } else {
                        let info = fsys::InstanceInfo {
                            moniker: SESSION_MONIKER.to_string(),
                            url: "fake".to_string(),
                            instance_id: None,
                            state: fsys::InstanceState::Unresolved,
                        };
                        let resolved = None;
                        Ok((info, resolved))
                    };
                    let _ = responder.send(&mut result);
                }
                r => {
                    panic!("didn't expect request: {:?}", r)
                }
            }
        })?;

        let facade = ModularFacade::new_with_proxies(
            session_launcher,
            session_restarter,
            lifecycle_controller,
            realm_query,
        );

        run_facade_fns(facade).await?;

        assert_eq!(
            SESSION_LAUNCH_CALL_COUNT.get(),
            expected_launch_count,
            "SESSION_LAUNCH_CALL_COUNT"
        );
        assert_eq!(
            DESTROY_CHILD_CALL_COUNT.get(),
            expected_destroy_count,
            "DESTROY_CHILD_CALL_COUNT"
        );
        assert_eq!(
            SESSION_RESTART_CALL_COUNT.get(),
            expected_restart_count,
            "SESSION_RESTART_CALL_COUNT"
        );

        Ok(())
    }

    #[fuchsia_async::run(2, test)]
    async fn test_kill_basemgr() -> Result<(), Error> {
        async fn kill_basemgr_steps(facade: ModularFacade) -> Result<(), Error> {
            assert_matches!(facade.is_basemgr_running().await, Ok(true));
            assert_matches!(facade.kill_basemgr().await, Ok(KillBasemgrResult::Success));
            Ok(())
        }

        test_facade(
            &kill_basemgr_steps,
            true, // is_basemgr_running
            0,    // SESSION_LAUNCH_CALL_COUNT
            1,    // DESTROY_CHILD_CALL_COUNT
            0,    // SESSION_RESTART_CALL_COUNT
        )
        .await
    }

    #[fuchsia_async::run(2, test)]
    async fn test_start_basemgr_v2_without_config() -> Result<(), Error> {
        async fn start_basemgr_v2_steps(facade: ModularFacade) -> Result<(), Error> {
            let start_basemgr_args = json!({
                "session_url": TEST_SESSION_URL,
            });
            assert_matches!(
                facade.start_basemgr(start_basemgr_args).await,
                Ok(BasemgrResult::Success)
            );
            Ok(())
        }

        test_facade(
            &start_basemgr_v2_steps,
            false, // is_basemgr_running
            1,     // SESSION_LAUNCH_CALL_COUNT
            0,     // DESTROY_CHILD_CALL_COUNT
            0,     // SESSION_RESTART_CALL_COUNT
        )
        .await
    }

    #[fuchsia_async::run(2, test)]
    async fn test_start_basemgr_v2_shutdown_existing() -> Result<(), Error> {
        async fn start_existing_steps(facade: ModularFacade) -> Result<(), Error> {
            let start_basemgr_args = json!({
                "session_url": TEST_SESSION_URL,
            });

            assert_matches!(facade.is_basemgr_running().await, Ok(true));
            // start_basemgr() will notice that there's an existing basemgr, destroy it, then launch
            // the new session.
            assert_matches!(
                facade.start_basemgr(start_basemgr_args).await,
                Ok(BasemgrResult::Success)
            );
            Ok(())
        }

        test_facade(
            &start_existing_steps,
            true, // is_basemgr_running
            1,    // SESSION_LAUNCH_CALL_COUNT
            1,    // DESTROY_CHILD_CALL_COUNT
            0,    // SESSION_RESTART_CALL_COUNT
        )
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_is_basemgr_running_not_running() -> Result<(), Error> {
        async fn not_running_steps(facade: ModularFacade) -> Result<(), Error> {
            assert_matches!(facade.is_basemgr_running().await, Ok(false));

            Ok(())
        }

        test_facade(
            &not_running_steps,
            false, // is_basemgr_running
            0,     // SESSION_LAUNCH_CALL_COUNT
            0,     // DESTROY_CHILD_CALL_COUNT
            0,     // SESSION_RESTART_CALL_COUNT
        )
        .await
    }

    #[fuchsia_async::run(2, test)]
    async fn test_is_basemgr_running_v2() -> Result<(), Error> {
        async fn is_running_steps(facade: ModularFacade) -> Result<(), Error> {
            assert_matches!(facade.is_basemgr_running().await, Ok(true));

            Ok(())
        }

        test_facade(
            &is_running_steps,
            true, // is_basemgr_running
            0,    // SESSION_LAUNCH_CALL_COUNT
            0,    // DESTROY_CHILD_CALL_COUNT
            0,    // SESSION_RESTART_CALL_COUNT
        )
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_restart_session() -> Result<(), Error> {
        async fn restart_steps(facade: ModularFacade) -> Result<(), Error> {
            assert_matches!(facade.restart_session().await, Ok(RestartSessionResult::Success));

            Ok(())
        }

        test_facade(
            &restart_steps,
            true, // is_basemgr_running
            0,    // SESSION_LAUNCH_CALL_COUNT
            0,    // DESTROY_CHILD_CALL_COUNT
            1,    // SESSION_RESTART_CALL_COUNT
        )
        .await
    }

    #[fuchsia_async::run(2, test)]
    async fn test_restart_session_does_not_destroy() -> Result<(), Error> {
        async fn restart_steps(facade: ModularFacade) -> Result<(), Error> {
            assert_matches!(facade.is_basemgr_running().await, Ok(true));

            assert_matches!(facade.restart_session().await, Ok(RestartSessionResult::Success));

            Ok(())
        }

        test_facade(
            &restart_steps,
            true, // is_basemgr_running
            0,    // SESSION_LAUNCH_CALL_COUNT
            0,    // DESTROY_CHILD_CALL_COUNT
            1,    // SESSION_RESTART_CALL_COUNT
        )
        .await
    }
}
