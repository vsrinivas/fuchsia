// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::adc::facade::AdcFacade;
use crate::server::Facade;
use anyhow::{bail, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for AdcFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "GetResolution" => {
                let result = self.get_resolution(args).await?;
                Ok(to_value(result)?)
            }
            "GetSample" => {
                let result = self.get_sample(args).await?;
                Ok(to_value(result)?)
            }
            "GetNormalizedSample" => {
                let result = self.get_normalized_sample(args).await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid AdcFacade FIDL method: {:?}", method),
        }
    }
}
