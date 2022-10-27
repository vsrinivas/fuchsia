// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::modular::{facade::ModularFacade, types::ModularMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for ModularFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            ModularMethod::RestartSession => {
                let result = self.restart_session().await?;
                Ok(to_value(result)?)
            }
            ModularMethod::StartBasemgr => {
                let result = self.start_basemgr(args).await?;
                Ok(to_value(result)?)
            }
            ModularMethod::KillBasemgr => {
                let result = self.kill_basemgr().await?;
                Ok(to_value(result)?)
            }
            ModularMethod::IsBasemgrRunning => {
                let result = self.is_basemgr_running().await?;
                Ok(to_value(result)?)
            }
        }
    }
}
