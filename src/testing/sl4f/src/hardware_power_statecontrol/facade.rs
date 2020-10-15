// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminProxy, RebootReason};
use fuchsia_component as app;
use fuchsia_syslog::macros::{fx_log_err, fx_log_info};

/// Perform Fuchsia Device Manager fidl operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct HardwarePowerStatecontrolFacade {}

impl HardwarePowerStatecontrolFacade {
    pub fn new() -> HardwarePowerStatecontrolFacade {
        HardwarePowerStatecontrolFacade {}
    }

    fn get_admin_proxy(&self) -> Result<AdminProxy, Error> {
        let tag = "HardwarePowerStatecontrolFacade";
        match app::client::connect_to_service::<AdminMarker>() {
            Ok(p) => Ok(p),
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to create device admin proxy: {:?}", err)
            ),
        }
    }

    /// Reboot the Fuchsia device
    pub async fn suspend_reboot(&self) -> Result<(), Error> {
        let tag = "HardwarePowerStatecontrolFacade::suspend_reboot";
        fx_log_info!("Executing Suspend: REBOOT");
        if let Err(err) = self.get_admin_proxy()?.reboot(RebootReason::UserRequest).await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to change power control state: {:?}", err)
            )
        }
        Ok(())
    }

    /// Reboot the Fuchsia device into the bootloader
    pub async fn suspend_reboot_bootloader(&self) -> Result<(), Error> {
        let tag = "HardwarePowerStatecontrolFacade::suspend_reboot_bootloader";
        fx_log_info!("Executing Suspend: REBOOT_BOOTLOADER");

        if let Err(err) = self.get_admin_proxy()?.reboot_to_bootloader().await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to change power control state: {:?}", err)
            )
        }
        Ok(())
    }

    /// Reboot the Fuchsia device into recovery
    pub async fn suspend_reboot_recovery(&self) -> Result<(), Error> {
        let tag = "HardwarePowerStatecontrolFacade::suspend_reboot_recovery";
        fx_log_info!("Executing Suspend: REBOOT_RECOVERY");
        if let Err(err) = self.get_admin_proxy()?.reboot_to_recovery().await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to change power control state: {:?}", err)
            )
        }
        Ok(())
    }

    /// Power off the Fuchsia device
    pub async fn suspend_poweroff(&self) -> Result<(), Error> {
        let tag = "HardwarePowerStatecontrolFacade::suspend_poweroff";
        fx_log_info!("Executing Suspend: POWEROFF");

        if let Err(err) = self.get_admin_proxy()?.poweroff().await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to change power control state: {:?}", err)
            )
        }
        Ok(())
    }

    /// Suspend MEXEC the Fuchsia device
    pub async fn suspend_mexec(&self) -> Result<(), Error> {
        let tag = "HardwarePowerStatecontrolFacade::suspend_mexec";
        fx_log_info!("Executing Suspend: MEXEC");

        if let Err(err) = self.get_admin_proxy()?.mexec().await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to change power control state: {:?}", err)
            )
        }
        Ok(())
    }

    /// RSuspend RAM on the Fuchsia device
    pub async fn suspend_ram(&self) -> Result<(), Error> {
        let tag = "HardwarePowerStatecontrolFacade::suspend_ram";
        fx_log_info!("Executing Suspend: SUSPEND_RAM");
        if let Err(err) = self.get_admin_proxy()?.suspend_to_ram().await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to change power control state: {:?}", err)
            )
        }
        Ok(())
    }
}
