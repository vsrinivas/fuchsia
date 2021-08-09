// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::common_utils::{buffer, fidl::connect_in_paths};
use crate::modular::types::{
    BasemgrResult, KillBasemgrResult, LaunchModRequest, RestartSessionResult, StartBasemgrRequest,
};
use anyhow::{Context, Error};
use fidl::endpoints::ProtocolMarker;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_mem as fmem;
use fidl_fuchsia_modular::{
    AddMod, Intent, PuppetMasterMarker, PuppetMasterProxy, StoryCommand, StoryPuppetMasterMarker,
    SurfaceArrangement, SurfaceDependency, SurfaceRelation,
};
use fidl_fuchsia_modular_internal as fmodular_internal;
use fidl_fuchsia_modular_session as fmodular_session;
use fidl_fuchsia_session as fsession;
use fidl_fuchsia_sys as fsys;
use fuchsia_async as fasync;
use fuchsia_component::{client, fuchsia_single_component_package_url};
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use fuchsia_zircon::HandleBased;
use futures::future;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use glob::glob;
use serde_json::{from_value, Value};
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, file::vmo::read_only_const,
    pseudo_directory,
};

/// Legacy component URL for basemgr.
const BASEMGR_LEGACY_URL: &str = fuchsia_single_component_package_url!("basemgr");

/// Glob pattern for the path to sessionmgr's debug services for sessionctl.
const SESSIONCTL_GLOB: &str = "/hub/c/sessionmgr.cmx/*/out/debug/sessionctl";

/// Glob pattern for the path to basemgr's debug service when basemgr is running as a legacy component.
const BASEMGR_DEBUG_LEGACY_GLOB: &str = "/hub/c/basemgr.cmx/*/out/debug/basemgr";

/// Glob pattern for the path to basemgr's debug service when basemgr is running as a v2 session.
const BASEMGR_DEBUG_SESSION_GLOB: &str = "/hub-v2/children/core/children/session-manager/children/\
    session:session/exec/expose/fuchsia.modular.internal.BasemgrDebug";

/// Glob pattern for the path to the `fuchsia.modular.session.Launcher service
/// exposed by basemgr when running as a v2 session.
const MODULAR_SESSION_LAUNCHER_GLOB: &str =
    "/hub-v2/children/core/children/session-manager/children/\
    session:session/exec/expose/fuchsia.modular.session.Launcher";

// Maximum number of story commands to send to a single Enqueue call.
const STORY_COMMAND_CHUNK_SIZE: usize = 32;

enum BasemgrRuntimeState {
    // basemgr is running as a legacy component.
    LegacyComponent,

    // basemgr is running as a v2 session.
    V2Session,
}

/// Returns true if sessionmgr is running.
fn is_sessionmgr_running() -> bool {
    // sessionmgr is running if its hub directory exists.
    glob(SESSIONCTL_GLOB).unwrap().find_map(Result::ok).is_some()
}

/// Returns the state of the currently running basemgr instance, or None if not running.
fn get_basemgr_runtime_state() -> Option<BasemgrRuntimeState> {
    if glob(BASEMGR_DEBUG_SESSION_GLOB).unwrap().find_map(Result::ok).is_some() {
        return Some(BasemgrRuntimeState::V2Session);
    }
    if glob(BASEMGR_DEBUG_LEGACY_GLOB).unwrap().find_map(Result::ok).is_some() {
        return Some(BasemgrRuntimeState::LegacyComponent);
    }
    None
}

/// Returns a BasemgrDebugProxy served by the currently running basemgr.
fn connect_to_basemgr_debug() -> Result<Option<fmodular_internal::BasemgrDebugProxy>, Error> {
    connect_in_paths::<fmodular_internal::BasemgrDebugMarker>(&[
        BASEMGR_DEBUG_SESSION_GLOB,
        BASEMGR_DEBUG_LEGACY_GLOB,
    ])
}

/// Returns a `fuchsia.modular.session.Launcher` served by the currently running session.
fn connect_to_modular_session_launcher() -> Result<Option<fmodular_session::LauncherProxy>, Error> {
    connect_in_paths::<fmodular_session::LauncherMarker>(&[MODULAR_SESSION_LAUNCHER_GLOB])
}

/// Returns a PuppetMasterProxy served by the currently running sessionmgr.
fn connect_to_puppet_master() -> Result<Option<PuppetMasterProxy>, Error> {
    let glob_path = format!("{}/{}", SESSIONCTL_GLOB, PuppetMasterMarker::NAME);
    connect_in_paths::<PuppetMasterMarker>(&[&glob_path])
}

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct ModularFacade {
    sys_launcher: fsys::LauncherProxy,
    session_launcher: fsession::LauncherProxy,
}

impl ModularFacade {
    pub fn new() -> ModularFacade {
        let sys_launcher = client::launcher().expect("failed to connect to fuchsia.sys.Launcher");
        let session_launcher = client::connect_to_protocol::<fsession::LauncherMarker>()
            .expect("failed to connect to fuchsia.session.Launcher");
        ModularFacade { sys_launcher, session_launcher }
    }

    pub fn new_with_proxies(
        sys_launcher: fsys::LauncherProxy,
        session_launcher: fsession::LauncherProxy,
    ) -> ModularFacade {
        ModularFacade { sys_launcher, session_launcher }
    }

    /// Restarts the currently running Modular session.
    pub async fn restart_session(&self) -> Result<RestartSessionResult, Error> {
        if !is_sessionmgr_running() {
            return Ok(RestartSessionResult::NoSessionToRestart);
        }
        let basemgr_debug = connect_to_basemgr_debug()?
            .ok_or_else(|| format_err!("Unable to connect to BasemgrDebug protocol"))?;
        basemgr_debug.restart_session().await?;
        Ok(RestartSessionResult::Success)
    }

    /// Facade to kill basemgr from Sl4f
    pub async fn kill_basemgr(&self) -> Result<KillBasemgrResult, Error> {
        match connect_to_basemgr_debug()? {
            Some(basemgr_debug) => {
                basemgr_debug.shutdown()?;
                Ok(KillBasemgrResult::Success)
            }
            None => Ok(KillBasemgrResult::NoBasemgrToKill),
        }
    }

    /// Starts a Modular session, either as a v2 session or by launching basemgr
    /// as a legacy component.
    ///
    /// If basemgr is already running, it will be shut down first.
    /// If `args.session_url` is provided, basemgr will be started as a session
    /// with the given URL. Otherwise, basemgr will be started as a legacy component.
    ///
    /// # Arguments
    /// * `args`: A serde_json Value parsed into [`StartBasemgrRequest`]
    pub async fn start_basemgr(&self, args: Value) -> Result<BasemgrResult, Error> {
        let req: StartBasemgrRequest =
            if args.is_null() { StartBasemgrRequest::default() } else { from_value(args)? };

        let config_json =
            req.config.map_or(None, |value| Some(serde_json::to_string(&value))).transpose()?;

        // If basemgr is running, shut it down before starting a new one.
        if get_basemgr_runtime_state().is_some() {
            connect_to_basemgr_debug()?
                .ok_or_else(|| format_err!("Unable to connect to BasemgrDebug protocol"))?
                .shutdown()?;
        }

        if let Some(session_url) = req.session_url {
            self.launch_session(&session_url).await?;

            if let Some(config_json) = config_json {
                self.launch_sessionmgr(&config_json).await?;
            }

            Ok(BasemgrResult::Success)
        } else {
            self.launch_basemgr_legacy(config_json.as_deref()).await
        }
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

    /// Launches an instance of sessionmgr with the given configuration using the
    /// `fuchsia.modular.session.Launcher` protocol exposed by a session.
    ///
    /// # Arguments
    /// * `config`: Modular configuration as a JSON string
    async fn launch_sessionmgr(&self, config: &str) -> Result<BasemgrResult, Error> {
        let mut config_buf: fmem::Buffer = buffer::try_from_bytes(config.as_bytes())?;

        connect_to_modular_session_launcher()?
            .ok_or_else(|| {
                format_err!("Unable to connect to fuchsia.modular.session.Launcher protocol")
            })?
            .launch_sessionmgr(&mut config_buf)?;

        Ok(BasemgrResult::Success)
    }

    /// Launches basemgr as a legacy component.
    ///
    /// # Arguments
    /// * `config`: Modular configuration as a JSON string
    async fn launch_basemgr_legacy(&self, config: Option<&str>) -> Result<BasemgrResult, Error> {
        let mut launch_options = client::LaunchOptions::new();

        // Provide the config to basemgr in its namespace as /config_override/data/startup.config.
        if let Some(config_str) = config {
            let (dir2_server, dir2_client) = zx::Channel::create()?;
            let config_str = config_str.to_string();
            fasync::Task::spawn(
                async move {

                    let scope = ExecutionScope::new();
                    let pkg_dir =
                        pseudo_directory! {"startup.config" => read_only_const(config_str.as_ref())};
                    pkg_dir.open(
                        scope.clone(),
                        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        vfs::path::Path::dot(),
                        ServerEnd::new(dir2_server),
                    );
                    scope.wait().await;
                    Ok::<(), Error>(())
                }
                .unwrap_or_else(|e: anyhow::Error| {
                    fx_log_err!("basemgr config pseudo dir stopped serving: {:?}", e)
                }),
            )
            .detach();
            launch_options.add_handle_to_namespace(
                "/config_override/data".to_string(),
                dir2_client.into_handle(),
            );
        } else {
            fx_log_warn!("Launching basemgr with default configuration");
        }

        // Launch basemgr.
        let basemgr = client::launch_with_options(
            &self.sys_launcher,
            BASEMGR_LEGACY_URL.to_string(),
            None,
            launch_options,
        )?;

        // Wait for basemgr's outgoing directory to be served.
        let event_stream = basemgr.controller().take_event_stream();
        event_stream
            .try_filter_map(|event| {
                let event = match event {
                    fsys::ComponentControllerEvent::OnDirectoryReady {} => {
                        Some(basemgr.controller().detach())
                    }
                    _ => None,
                };
                future::ready(Ok(event))
            })
            .next()
            .await;
        Ok(BasemgrResult::Success)
    }

    /// Facade to launch mod from Sl4f
    /// # Arguments
    /// * `args`: will be parsed to LaunchModRequest
    /// * `mod_url`: url of the mod
    /// * `mod_name`: same as mod_url if nothing is passed in
    /// * `story_name`: same as mod_url if nothing is passed in
    pub async fn launch_mod(&self, args: Value) -> Result<BasemgrResult, Error> {
        let req: LaunchModRequest = from_value(args)?;

        // Building the component url from the name of component.
        let mod_url = match req.mod_url {
            Some(x) => {
                let url = match x.find(":") {
                    Some(_y) => x.to_string(),
                    None => format!("fuchsia-pkg://fuchsia.com/{}#meta/{}.cmx", x, x).to_string(),
                };
                fx_log_info!("Launching mod: {}", url);
                url
            }
            None => return Err(format_err!("Missing MOD_URL to launch")),
        };

        let mod_name = match req.mod_name {
            Some(x) => x.to_string(),
            None => {
                fx_log_info!("No mod_name specified, using auto-generated mod_name: {}", &mod_url);
                mod_url.clone()
            }
        };

        let story_name = match req.story_name {
            Some(x) => x.to_string(),
            None => {
                fx_log_info!(
                    "No story_name specified, using auto-generated story_name: {}",
                    &mod_url
                );
                mod_url.clone()
            }
        };

        let mut intent = Intent { action: None, handler: None, parameters: None };
        intent.handler = Some(mod_url.clone());
        let mut commands = vec![];

        //set it to default value of surface relation
        let sur_rel = SurfaceRelation {
            dependency: SurfaceDependency::None,
            arrangement: SurfaceArrangement::None,
            emphasis: 1.0,
        };
        let add_mod = AddMod {
            intent,
            mod_name_transitional: Some(mod_name.clone()),
            mod_name: vec![mod_name.clone()],
            surface_parent_mod_name: None,
            surface_relation: sur_rel,
        };
        commands.push(StoryCommand::AddMod(add_mod));

        let puppet_master = connect_to_puppet_master()?
            .ok_or_else(|| format_err!("Unable to connect to PuppetMaster protocol"))?;
        let (proxy, server) = fidl::endpoints::create_proxy::<StoryPuppetMasterMarker>()?;
        puppet_master.control_story(story_name.as_str(), server)?;

        // Chunk the command list into fixed size chunks and cast it to iterator to
        // meet fidl's specification
        for chunk in commands.chunks_mut(STORY_COMMAND_CHUNK_SIZE).into_iter() {
            proxy.enqueue(&mut chunk.into_iter()).context("Failed to enqueue commands")?;
        }
        proxy.execute().await?;

        Ok(BasemgrResult::Success)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::common_utils::namespace_binder::NamespaceBinder,
        fidl::endpoints::spawn_stream_handler,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_modular_internal as fmodular_internal,
        fidl_fuchsia_modular_session as fmodular_session,
        futures::channel::mpsc,
        futures::SinkExt,
        io_util,
        lazy_static::lazy_static,
        matches::assert_matches,
        serde_json::json,
        std::sync::{Arc, Mutex},
        test_util::Counter,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_basemgr_legacy() -> Result<(), Error> {
        lazy_static! {
            static ref LAUNCH_CALL_COUNT: Counter = Counter::new(0);
            static ref TEST_MODULAR_CONFIG: serde_json::Value = json!({ "hello": "world" });
        }

        let sys_launcher = spawn_stream_handler(move |launcher_request| async move {
            match launcher_request {
                fsys::LauncherRequest::CreateComponent {
                    launch_info: fsys::LaunchInfo { url, flat_namespace, .. },
                    ..
                } => {
                    assert_eq!(url, BASEMGR_LEGACY_URL);

                    let flat_namespace = flat_namespace.unwrap();
                    assert_eq!(flat_namespace.paths.len(), 1);
                    assert_eq!(flat_namespace.directories.len(), 1);

                    let config_dir = flat_namespace.directories.into_iter().next().unwrap();

                    let dir_proxy =
                        ClientEnd::<fio::DirectoryMarker>::new(config_dir).into_proxy().unwrap();

                    let file_proxy = io_util::directory::open_file(
                        &dir_proxy,
                        "startup.config",
                        fio::OPEN_RIGHT_READABLE,
                    )
                    .await
                    .unwrap();

                    let config_str = io_util::file::read_to_string(&file_proxy).await.unwrap();

                    let got_config: serde_json::Value = serde_json::from_str(&config_str).unwrap();
                    let expected_config: serde_json::Value = TEST_MODULAR_CONFIG.clone();
                    assert_eq!(got_config, expected_config);

                    LAUNCH_CALL_COUNT.inc();
                }
            }
        })?;

        let session_launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("fuchsia.session.Launcher should not be called as this test does not provide a session_url");
        })?;

        let facade = ModularFacade::new_with_proxies(sys_launcher, session_launcher);

        let start_basemgr_args = json!({
            "config": { "hello": "world" },
        });
        assert_matches!(facade.start_basemgr(start_basemgr_args).await, Ok(BasemgrResult::Success));
        assert_eq!(LAUNCH_CALL_COUNT.get(), 1);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_basemgr_legacy_without_args() -> Result<(), Error> {
        lazy_static! {
            static ref LAUNCH_CALL_COUNT: Counter = Counter::new(0);
        }

        let sys_launcher = spawn_stream_handler(move |launcher_request| async move {
            match launcher_request {
                fsys::LauncherRequest::CreateComponent {
                    launch_info: fsys::LaunchInfo { url, flat_namespace, .. },
                    ..
                } => {
                    assert_eq!(url, BASEMGR_LEGACY_URL);
                    assert!(flat_namespace.is_none());
                    LAUNCH_CALL_COUNT.inc();
                }
            }
        })?;

        let session_launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("fuchsia.session.Launcher should not be called as this test does not provide a session_url");
        })?;

        let facade = ModularFacade::new_with_proxies(sys_launcher, session_launcher);

        assert_matches!(facade.start_basemgr(json!(null)).await, Ok(BasemgrResult::Success));
        assert_eq!(LAUNCH_CALL_COUNT.get(), 1);

        Ok(())
    }

    #[fuchsia_async::run(2, test)]
    async fn test_start_basemgr_v2_launches_session() -> Result<(), Error> {
        const TEST_SESSION_URL: &str =
            "fuchsia-pkg://fuchsia.com/test_session#meta/test_session.cm";

        lazy_static! {
            static ref TEST_MODULAR_CONFIG: serde_json::Value = json!({ "hello": "world" });
            static ref SESSION_LAUNCH_CALL_COUNT: Counter = Counter::new(0);
        }

        let sys_launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!(
                "basemgr should not be started as a legacy component if a session_url is provided"
            );
        })?;

        let scope = ExecutionScope::new();
        let ns = Arc::new(Mutex::new(NamespaceBinder::new(scope)));

        let (called_modular_launch_tx, mut called_modular_launch_rx) = mpsc::channel(0);

        let modular_session_launcher =
            vfs::service::host(move |mut stream: fmodular_session::LauncherRequestStream| {
                let mut called_modular_launch_tx = called_modular_launch_tx.clone();
                async move {
                    while let Ok(Some(request)) = stream.try_next().await {
                        match request {
                            fmodular_session::LauncherRequest::LaunchSessionmgr {
                                config, ..
                            } => {
                                let config_bytes = buffer::try_into_bytes(config).unwrap();
                                let got_config: serde_json::Value =
                                    serde_json::from_slice(&config_bytes).unwrap();
                                let expected_config: serde_json::Value =
                                    TEST_MODULAR_CONFIG.clone();
                                assert_eq!(got_config, expected_config);

                                called_modular_launch_tx
                                    .send(())
                                    .await
                                    .expect("could not send on channel");
                            }
                            fmodular_session::LauncherRequest::LaunchSessionmgrWithServices {
                                ..
                            } => {
                                panic!("LaunchSessionmgrWithServices should not be called");
                            }
                        }
                    }
                }
            });

        let session_launcher = spawn_stream_handler(move |launcher_request| {
            let ns = ns.clone();
            let modular_session_launcher = modular_session_launcher.clone();
            async move {
                match launcher_request {
                    fsession::LauncherRequest::Launch { configuration, responder } => {
                        assert!(configuration.session_url.is_some());
                        let session_url = configuration.session_url.unwrap();
                        assert!(session_url == TEST_SESSION_URL.to_string());

                        // Serve the `fuchsia.modular.session.Launcher` protocol that `start_basemgr`
                        // should use to launch sessionmgr, instead of launching basemgr as a legacy component.
                        // This is served here to simulate the session starting.
                        ns.lock()
                            .unwrap()
                            .bind_at_path(MODULAR_SESSION_LAUNCHER_GLOB, modular_session_launcher)
                            .expect("failed to bind modular session Launcher");

                        SESSION_LAUNCH_CALL_COUNT.inc();
                        let _ = responder.send(&mut Ok(()));
                    }
                }
            }
        })?;

        let facade = ModularFacade::new_with_proxies(sys_launcher, session_launcher);

        let start_basemgr_args = json!({
            "config": { "hello": "world" },
            "session_url": TEST_SESSION_URL
        });
        assert_matches!(facade.start_basemgr(start_basemgr_args).await, Ok(BasemgrResult::Success));

        // The session should have been launched.
        assert_eq!(SESSION_LAUNCH_CALL_COUNT.get(), 1);

        // LaunchSessionmgr should have been called.
        assert_eq!(called_modular_launch_rx.next().await, Some(()));

        Ok(())
    }

    #[fuchsia_async::run(2, test)]
    async fn test_start_basemgr_v2_without_config() -> Result<(), Error> {
        const TEST_SESSION_URL: &str =
            "fuchsia-pkg://fuchsia.com/test_session#meta/test_session.cm";

        lazy_static! {
            static ref SESSION_LAUNCH_CALL_COUNT: Counter = Counter::new(0);
        }

        let sys_launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!(
                "basemgr should not be started as a legacy component if a session_url is provided"
            );
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

        let facade = ModularFacade::new_with_proxies(sys_launcher, session_launcher);

        let scope = ExecutionScope::new();
        let mut ns = NamespaceBinder::new(scope);

        // Serve the `fuchsia.modular.session.Launcher` protocol.
        let modular_session_launcher = vfs::service::host(
            |mut _stream: fmodular_session::LauncherRequestStream| async {
                panic!("fuchsia.modular.session.Launch should not be called because no config is provided");
            },
        );
        ns.bind_at_path(MODULAR_SESSION_LAUNCHER_GLOB, modular_session_launcher)?;

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

        let sys_launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!(
                "basemgr should not be started as a legacy component if a session_url is provided"
            );
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

        let facade = ModularFacade::new_with_proxies(sys_launcher, session_launcher);

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
                                    "BasemgrDebug methods other than Shutdown shold not be called"
                                );
                            }
                        }
                    }
                }
            });
        // Serve the `fuchsia.modular.session.Launcher` protocol.
        let modular_session_launcher = vfs::service::host(
            |mut _stream: fmodular_session::LauncherRequestStream| async {
                panic!("fuchsia.modular.session.Launch should not be called because no config is provided");
            },
        );
        ns.bind_at_path(BASEMGR_DEBUG_SESSION_GLOB, basemgr_debug)?;
        ns.bind_at_path(MODULAR_SESSION_LAUNCHER_GLOB, modular_session_launcher)?;

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
}
