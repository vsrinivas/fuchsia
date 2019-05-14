// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logging::facade::LoggingFacade;
use crate::logging::types::LoggingMethod;
use failure::{bail, Error};
use serde_json::{to_value, Value};
use std::sync::Arc;

pub async fn logging_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<LoggingFacade>,
) -> Result<Value, Error> {
    match LoggingMethod::from_str(&method_name) {
        LoggingMethod::LogErr => {
            let message = match args.get("message") {
                Some(m) => m.to_string(),
                None => bail!("Expected a serde_json Value \'message\'."),
            };
            let result = await!(facade.log_err(message))?;
            Ok(to_value(result)?)
        }
        LoggingMethod::LogInfo => {
            let message = match args.get("message") {
                Some(m) => m.to_string(),
                None => bail!("Expected a serde_json Value \'message\'."),
            };
            let result = await!(facade.log_info(message))?;
            Ok(to_value(result)?)
        }
        LoggingMethod::LogWarn => {
            let message = match args.get("message") {
                Some(m) => m.to_string(),
                None => bail!("Expected a serde_json Value \'message\'."),
            };
            let result = await!(facade.log_warn(message))?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid Logging Facade FIDL method: {:?}", method_name),
    }
}
