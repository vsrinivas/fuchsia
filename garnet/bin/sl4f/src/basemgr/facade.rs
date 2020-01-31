// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::basemgr::types::{RestartSessionResult, StartBasemgrResult};
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io::{MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
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
use serde_json::Value;
use std::iter;
use std::path::PathBuf;
const COMPONENT_URL: &str = fuchsia_single_component_package_url!("basemgr");

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
        let glob_path = "/hub/c/sessionmgr.cmx/*/out/debug/sessionctl";
        Ok(glob(glob_path)?.filter_map(|entry| entry.ok()).collect())
    }

    fn discover_basemgr_service(&self) -> Result<Option<BasemgrDebugSynchronousProxy>, Error> {
        let glob_path = "/hub/c/basemgr.cmx/*/out/debug/basemgr";
        let found_path = glob(glob_path)?.filter_map(|entry| entry.ok()).next();
        match found_path {
            Some(path) => {
                let (client, server) = zx::Channel::create()?;
                fdio::service_connect(path.to_string_lossy().as_ref(), server)?;
                Ok(Some(BasemgrDebugSynchronousProxy::new(client)))
            }
            None => Ok(None),
        }
    }

    /// Facade to launch basemgr from Sl4f
    /// Use default config if custom config is not provided.
    pub async fn start_basemgr(&self, args: Value) -> Result<StartBasemgrResult, Error> {
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
                fasync::spawn(async move {
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

                );
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
        Ok(StartBasemgrResult::Success)
    }
}
