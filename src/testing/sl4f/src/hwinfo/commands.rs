// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

use crate::hwinfo::facade::HwinfoFacade;

#[async_trait(?Send)]
impl Facade for HwinfoFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "HwinfoGetDeviceInfo" => {
                let result = self.get_device_info().await?;
                Ok(to_value(result)?)
            }
            "HwinfoGetProductInfo" => {
                let result = self.get_product_info().await?;
                Ok(to_value(result)?)
            }
            "HwinfoGetBoardInfo" => {
                let result = self.get_board_info().await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid Hwinfo FIDL method: {:?}", method),
        }
    }
}
