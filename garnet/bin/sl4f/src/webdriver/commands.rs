// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

use crate::webdriver::facade::WebdriverFacade;

impl Facade for WebdriverFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        webdriver_method_to_fidl(method, args, self).boxed_local()
    }
}

/// Forwards SL4F Webdriver commands to Webdriver facade.
async fn webdriver_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: &WebdriverFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "EnableDevTools" => {
            let result = facade.enable_dev_tools().await?;
            Ok(to_value(result)?)
        }
        "GetDevToolsPorts" => {
            let result = facade.get_dev_tools_ports().await?;
            Ok(to_value(result)?)
        }
        _ => return Err(format_err!("Invalid WebDriver facade method: {:?}", method_name)),
    }
}
