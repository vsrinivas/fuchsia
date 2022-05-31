// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::power::facade::PowerFacade;
use crate::server::Facade;
use anyhow::{bail, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for PowerFacade {
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
                let result = self.stop_logging().await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid PowerFacade FIDL method: {:?}", method),
        }
    }
}
