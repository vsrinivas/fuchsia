// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

use crate::webdriver::facade::WebdriverFacade;

#[async_trait(?Send)]
impl Facade for WebdriverFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "EnableDevTools" => {
                let result = self.enable_dev_tools().await?;
                Ok(to_value(result)?)
            }
            "GetDevToolsPorts" => {
                let result = self.get_dev_tools_ports().await?;
                Ok(to_value(result)?)
            }
            _ => return Err(format_err!("Invalid WebDriver facade method: {:?}", method)),
        }
    }
}
