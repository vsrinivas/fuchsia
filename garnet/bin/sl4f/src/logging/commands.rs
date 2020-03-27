// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logging::facade::LoggingFacade;
use crate::logging::types::LoggingMethod;
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for LoggingFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match LoggingMethod::from_str(&method) {
            LoggingMethod::LogErr => {
                let message = match args.get("message") {
                    Some(m) => m.to_string(),
                    None => return Err(format_err!("Expected a serde_json Value \'message\'.")),
                };
                let result = self.log_err(message).await?;
                Ok(to_value(result)?)
            }
            LoggingMethod::LogInfo => {
                let message = match args.get("message") {
                    Some(m) => m.to_string(),
                    None => return Err(format_err!("Expected a serde_json Value \'message\'.")),
                };
                let result = self.log_info(message).await?;
                Ok(to_value(result)?)
            }
            LoggingMethod::LogWarn => {
                let message = match args.get("message") {
                    Some(m) => m.to_string(),
                    None => return Err(format_err!("Expected a serde_json Value \'message\'.")),
                };
                let result = self.log_warn(message).await?;
                Ok(to_value(result)?)
            }
            _ => return Err(format_err!("Invalid Logging Facade FIDL method: {:?}", method)),
        }
    }
}
