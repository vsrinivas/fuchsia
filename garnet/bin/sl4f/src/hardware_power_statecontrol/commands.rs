// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

use crate::hardware_power_statecontrol::facade::HardwarePowerStatecontrolFacade;

#[async_trait(?Send)]
impl Facade for HardwarePowerStatecontrolFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "SuspendReboot" => {
                let result = self.suspend_reboot().await?;
                Ok(to_value(result)?)
            }
            "SuspendRebootBootloader" => {
                let result = self.suspend_reboot_bootloader().await?;
                Ok(to_value(result)?)
            }
            "SuspendRebootRecovery" => {
                let result = self.suspend_reboot_recovery().await?;
                Ok(to_value(result)?)
            }
            "SuspendPoweroff" => {
                let result = self.suspend_poweroff().await?;
                Ok(to_value(result)?)
            }
            "SuspendMexec" => {
                let result = self.suspend_mexec().await?;
                Ok(to_value(result)?)
            }
            "SuspendRam" => {
                let result = self.suspend_ram().await?;
                Ok(to_value(result)?)
            }
            _ => bail!("Invalid HardwarePowerStatecontrolFacade FIDL method: {:?}", method),
        }
    }
}
