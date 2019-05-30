// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error};

use serde_json::{to_value, Value};
use std::sync::Arc;

use crate::webdriver::facade::WebdriverFacade;

/// Forwards SL4F Webdriver commands to Webdriver facade.
pub async fn webdriver_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: Arc<WebdriverFacade>,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "EnableDevTools" => {
            let result = await!(facade.enable_dev_tools())?;
            Ok(to_value(result)?)
        }
        "GetDevToolsPorts" => {
            let result = await!(facade.get_dev_tools_ports())?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid WebDriver facade method: {:?}", method_name),
    }
}
