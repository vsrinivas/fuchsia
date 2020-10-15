// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{facade::PaverFacade, types::Method};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{from_value, to_value, Value};

#[async_trait(?Send)]
impl Facade for PaverFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            Method::QueryActiveConfiguration => {
                let result = self.query_active_configuration().await?;
                Ok(to_value(result)?)
            }
            Method::QueryCurrentConfiguration => {
                let result = self.query_current_configuration().await?;
                Ok(to_value(result)?)
            }
            Method::QueryConfigurationStatus => {
                let result = self.query_configuration_status(from_value(args)?).await?;
                Ok(to_value(result)?)
            }
            Method::ReadAsset => {
                let result = self.read_asset(from_value(args)?).await?;
                Ok(to_value(result)?)
            }
        }
    }
}
