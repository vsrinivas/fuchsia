// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ram::facade::RamFacade;
use crate::ram::types::{RamMethod, SerializableBandwidthMeasurementConfig};
use crate::server::Facade;
use anyhow::{Context, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for RamFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match RamMethod::from(method.as_ref()) {
            RamMethod::MeasureBandwidth => {
                let arg_val =
                    args.get("values").context("values tag not found in request")?.clone();
                let val: SerializableBandwidthMeasurementConfig = serde_json::from_value(arg_val)?;
                let result = self.measure_bandwidth(val).await?;
                Ok(to_value(result)?)
            }
            RamMethod::GetDdrWindowingResults => {
                let result = self.get_ddr_windowing_results().await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid Ram Facade FIDL method: {:?}", method),
        }
    }
}
