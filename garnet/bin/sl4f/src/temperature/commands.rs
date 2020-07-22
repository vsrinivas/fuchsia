// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::temperature::facade::TemperatureFacade;
use anyhow::{bail, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for TemperatureFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "GetTemperatureCelsius" => {
                let result = self.get_temperature_celsius(args).await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid TemperatureFacade FIDL method: {:?}", method),
        }
    }
}
