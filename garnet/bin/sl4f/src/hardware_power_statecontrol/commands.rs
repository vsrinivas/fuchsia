// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

use crate::hardware_power_statecontrol::facade::HardwarePowerStatecontrolFacade;

impl Facade for HardwarePowerStatecontrolFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        hardware_power_statecontrol_method_to_fidl(method, args, self).boxed_local()
    }
}

async fn hardware_power_statecontrol_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: &HardwarePowerStatecontrolFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "SuspendReboot" => {
            let result = facade.suspend_reboot().await?;
            Ok(to_value(result)?)
        }
        "SuspendRebootBootloader" => {
            let result = facade.suspend_reboot_bootloader().await?;
            Ok(to_value(result)?)
        }
        "SuspendRebootRecovery" => {
            let result = facade.suspend_reboot_recovery().await?;
            Ok(to_value(result)?)
        }
        "SuspendPoweroff" => {
            let result = facade.suspend_poweroff().await?;
            Ok(to_value(result)?)
        }
        "SuspendMexec" => {
            let result = facade.suspend_mexec().await?;
            Ok(to_value(result)?)
        }
        "SuspendRam" => {
            let result = facade.suspend_ram().await?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid HardwarePowerStatecontrolFacade FIDL method: {:?}", method_name),
    }
}
