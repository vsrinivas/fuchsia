// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::battery_simulator::facade::BatterySimulatorFacade;
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for BatterySimulatorFacade {
    async fn handle_request(&self, method_name: String, arg: Value) -> Result<Value, Error> {
        match method_name.as_ref() {
            "SimulatorInit" => {
                let result = self.init_proxy(None)?;
                Ok(to_value(result)?)
            }
            "DisconnectRealBattery" => {
                let result = self.disconnect_real_battery()?;
                Ok(to_value(result)?)
            }
            "ReconnectRealBattery" => {
                let result = self.reconnect_real_battery()?;
                Ok(to_value(result)?)
            }
            "ChargeSource" => {
                let result = self.set_charge_source(arg)?;
                Ok(to_value(result)?)
            }
            "BatteryPercentage" => {
                let result = self.set_battery_percentage(arg)?;
                Ok(to_value(result)?)
            }
            "BatteryStatus" => {
                let result = self.set_battery_status(arg)?;
                Ok(to_value(result)?)
            }
            "ChargeStatus" => {
                let result = self.set_charge_status(arg)?;
                Ok(to_value(result)?)
            }
            "LevelStatus" => {
                let result = self.set_level_status(arg)?;
                Ok(to_value(result)?)
            }
            "TimeRemaining" => {
                let result = self.set_time_remaining(arg)?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid BatterySimulatorFacade FIDL method: {:?}", method_name),
        }
    }
}
