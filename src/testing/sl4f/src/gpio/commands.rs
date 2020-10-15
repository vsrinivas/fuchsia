// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::gpio::facade::GpioFacade;
use crate::gpio::types::{
    ConfigInRequest, ConfigOutRequest, GpioMethod, ReadRequest, SetDriveStrengthRequest,
    WriteRequest,
};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::convert::TryFrom;

#[async_trait(?Send)]
impl Facade for GpioFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match GpioMethod::try_from((method.as_str(), args))? {
            GpioMethod::ConfigIn(ConfigInRequest { pin, flags }) => {
                let result = self.config_in(pin, flags).await?;
                Ok(to_value(result)?)
            }

            GpioMethod::ConfigOut(ConfigOutRequest { pin, value }) => {
                let result = self.config_out(pin, value).await?;
                Ok(to_value(result)?)
            }

            GpioMethod::Read(ReadRequest { pin }) => {
                let result = self.read(pin).await?;
                Ok(to_value(result)?)
            }

            GpioMethod::Write(WriteRequest { pin, value }) => {
                let result = self.write(pin, value).await?;
                Ok(to_value(result)?)
            }

            GpioMethod::SetDriveStrength(SetDriveStrengthRequest { pin, ds_ua }) => {
                let result = self.set_drive_strength(pin, ds_ua).await?;
                Ok(to_value(result)?)
            }

            _ => bail!("Invalid Gpio Facade FIDL method: {:?}", method),
        }
    }
}
