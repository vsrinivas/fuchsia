// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::system_metrics::facade::SystemMetricsFacade;
use anyhow::{bail, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for SystemMetricsFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "StartLogging" => {
                let result = self.start_logging(args).await?;
                Ok(to_value(result)?)
            }
            "StartLoggingForever" => {
                let result = self.start_logging_forever(args).await?;
                Ok(to_value(result)?)
            }
            "StopLogging" => {
                let result = self.stop_logging(args).await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid SystemMetricsFacade FIDL method: {:?}", method),
        }
    }
}
