// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::basemgr::types::{
    BasemgrResult, KillBasemgrResult, LaunchModRequest, RestartSessionResult,
};
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_io::{MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use fidl_fuchsia_modular::{
    AddMod, Intent, PuppetMasterMarker, PuppetMasterSynchronousProxy, StoryCommand,
    StoryPuppetMasterMarker, SurfaceArrangement, SurfaceDependency, SurfaceRelation,
};
use fidl_fuchsia_modular_internal::BasemgrDebugSynchronousProxy;
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_async as fasync;
use fuchsia_component::{client, fuchsia_single_component_package_url};
use fuchsia_syslog::macros::*;
use fuchsia_vfs_pseudo_fs::{
    directory::entry::DirectoryEntry, file::simple::read_only_str, pseudo_directory,
};
use fuchsia_zircon as zx;
use fuchsia_zircon::HandleBased;
use futures::future;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use glob::glob;
use serde_json::{from_value, Value};
use std::iter;
use std::path::PathBuf;
const COMPONENT_URL: &str = fuchsia_single_component_package_url!("basemgr");
const SESSIONCTL_URL: &str = "/hub/c/sessionmgr.cmx/*/out/debug/sessionctl";
const BASEMGR_URL: &str = "/hub/c/basemgr.cmx/*/out/debug/basemgr";
const CHUNK_SIZE: usize = 32;

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct BaseManagerFacade {}

impl BaseManagerFacade {
    pub fn new() -> BaseManagerFacade {
        BaseManagerFacade {}
    }

    /// Discovers a |BasemgrDebug| service published by
    /// FuchsiaModularInternal and uses it restart the ongoing session
    pub async fn restart_session(&self) -> Result<RestartSessionResult, Error> {
        if self.find_all_sessions()?.is_empty() {
            return Ok(RestartSessionResult::NoSessionToRestart);
        }
        let mut basemgr_proxy = match self.discover_basemgr_service()? {
            Some(proxy) => proxy,
            None => return Err(format_err!("Unable to connect to Base Manager Service")),
        };
        basemgr_proxy.restart_session(zx::Time::after(zx::Duration::from_seconds(120)))?;
        Ok(RestartSessionResult::Success)
    }

    fn find_all_sessions(&self) -> Result<Vec<PathBuf>, Error> {
        Ok(glob(SESSIONCTL_URL)?.filter_map(|entry| entry.ok()).collect())
    }

    fn discover_basemgr_service(&self) -> Result<Option<BasemgrDebugSynchronousProxy>, Error> {
        let found_path = glob(BASEMGR_URL)?.filter_map(|entry| entry.ok()).next();
        match found_path {
            Some(path) => {
                let (client, server) = zx::Channel::create()?;
                fdio::service_connect(path.to_string_lossy().as_ref(), server)?;
                Ok(Some(BasemgrDebugSynchronousProxy::new(client)))
            }
            None => Ok(None),
        }
    }

    /// Facade to kill basemgr from Sl4f
    pub async fn kill_basemgr(&self) -> Result<KillBasemgrResult, Error> {
        match self.discover_basemgr_service()? {
            Some(mut proxy) => {
                proxy.shutdown()?;
                Ok(KillBasemgrResult::Success)
            }
            None => Ok(KillBasemgrResult::NoBasemgrToKill),
        }
    }

    /// Facade to launch basemgr from Sl4f
    /// Use default config if custom config is not provided.
    pub async fn start_basemgr(&self, args: Value) -> Result<BasemgrResult, Error> {
        match self.discover_basemgr_service()? {
            Some(mut proxy) => {
                proxy.shutdown()?;
            }
            None => {}
        };
        let mut launch_options = client::LaunchOptions::new();
        // if we cannot get "basemgr" which means there's no custom config or it's not valid, we use default config
        // host the custom startup.config in /config_override/data
        match args.get("config") {
            Some(config) => {
                let con_str = config.to_string();
                let (dir2_server, dir2_client) = zx::Channel::create()?;
                fasync::Task::spawn(async move {
                    let mut pkg_dir = pseudo_directory! {"startup.config" => read_only_str(|| Ok(con_str.to_string()))};
                    pkg_dir.open(
                        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                        MODE_TYPE_DIRECTORY,
                        &mut iter::empty(),
                        ServerEnd::new(dir2_server),
                    );
                    pkg_dir.await;
                    Ok::<(), Error>(())
                }.unwrap_or_else(|e: anyhow::Error| fx_log_err!("Psuedo dir stopped serving: {:?}", e)),

                ).detach();
                launch_options.add_handle_to_namespace(
                    "/config_override/data".to_string(),
                    dir2_client.into_handle(),
                );
            }
            None => {}
        }

        //launch basemgr
        let tag = "BaseManagerFacade::start_basemgr";
        let launcher = match client::launcher() {
            Ok(r) => r,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get launcher service: {}", err)
            ),
        };
        let basemgr = client::launch_with_options(
            &launcher,
            COMPONENT_URL.to_string(),
            None,
            launch_options,
        )?;

        // detach when basemgr out dir is ready as it's a signal
        let event_stream = basemgr.controller().take_event_stream();
        event_stream
            .try_filter_map(|event| {
                let event = match event {
                    ComponentControllerEvent::OnDirectoryReady {} => {
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

    /// connect to puppetMasterSynchronousProxy
    fn discover_puppet_master(&self) -> Result<Option<PuppetMasterSynchronousProxy>, Error> {
        let glob_path = format!("{}/{}", SESSIONCTL_URL, PuppetMasterMarker::NAME);
        let found_path = glob(glob_path.as_str())?.filter_map(|entry| entry.ok()).next();
        match found_path {
            Some(path) => {
                let (client, server) = zx::Channel::create()?;
                fdio::service_connect(path.to_string_lossy().as_ref(), server)?;
                Ok(Some(PuppetMasterSynchronousProxy::new(client)))
            }
            None => Ok(None),
        }
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
                fx_log_info!("Launching mod {} in Basemgr Facade.", url);
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
            intent: intent,
            mod_name_transitional: Some(mod_name.clone()),
            mod_name: vec![mod_name.clone()],
            surface_parent_mod_name: None,
            surface_relation: sur_rel,
        };
        commands.push(StoryCommand::AddMod(add_mod));

        let mut puppet_master = match self.discover_puppet_master()? {
            Some(proxy) => proxy,
            None => return Err(format_err!("Unable to connect to Puppet Master Service")),
        };
        let (proxy, server) = fidl::endpoints::create_proxy::<StoryPuppetMasterMarker>()?;
        puppet_master.control_story(story_name.as_str(), server)?;

        // Chunk the command list into fixed size chunks and cast it to iterator to
        // meet fidl's specification
        for chunk in commands.chunks_mut(CHUNK_SIZE).into_iter() {
            proxy.enqueue(&mut chunk.into_iter()).expect("Session failed to enqueue commands");
        }
        proxy.execute().await?;

        Ok(BasemgrResult::Success)
    }
}
