// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_zircon as zx;
use glob::glob;

use crate::basemgr::types::RestartSessionResult;
use fidl_fuchsia_modular_internal::BasemgrDebugSynchronousProxy;

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
        let mut basemgr_proxy = match self.discover_basemgr_service()? {
            Some(proxy) => proxy,
            None => bail!("Unable to connect to Base Manager Service"),
        };
        basemgr_proxy.restart_session(zx::Time::after(zx::Duration::from_seconds(30)))?;
        Ok(RestartSessionResult::Success)
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
}
