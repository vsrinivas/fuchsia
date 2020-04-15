// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::basemgr::{facade::BaseManagerFacade, types::BaseManagerMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for BaseManagerFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            BaseManagerMethod::RestartSession => {
                let result = self.restart_session().await?;
                Ok(to_value(result)?)
            }
            BaseManagerMethod::StartBasemgr => {
                let result = self.start_basemgr(args).await?;
                Ok(to_value(result)?)
            }
            BaseManagerMethod::KillBasemgr => {
                let result = self.kill_basemgr().await?;
                Ok(to_value(result)?)
            }
            BaseManagerMethod::LaunchMod => {
                let result = self.launch_mod(args).await?;
                Ok(to_value(result)?)
            }
        }
    }
}
