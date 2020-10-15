// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl_fuchsia_power as fpower;
use fidl_fuchsia_power_test as spower;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::macros::{fx_log_err, fx_log_warn};
use fuchsia_zircon::Status;
use parking_lot::RwLock;
use serde_json::Value;
use std::time::Duration;

#[derive(Debug)]
struct InnerBatterySimulatorFacade {
    proxy: Option<spower::BatterySimulatorProxy>,
}
#[derive(Debug)]
pub struct BatterySimulatorFacade {
    inner: RwLock<InnerBatterySimulatorFacade>,
}

static TAG: &str = "BatterySimulatorFacade";

impl BatterySimulatorFacade {
    pub fn new() -> Self {
        BatterySimulatorFacade { inner: RwLock::new(InnerBatterySimulatorFacade { proxy: None }) }
    }

    /// Initialize proxy to perform changes
    /// # Arguments
    /// * 'given_proxy' - An optional proxy for testing purposes
    pub fn init_proxy(
        &self,
        given_proxy: Option<spower::BatterySimulatorProxy>,
    ) -> Result<(), Error> {
        if given_proxy.is_none() {
            let proxy = self.create_proxy()?;
            self.inner.write().proxy = Some(proxy);
        } else {
            self.inner.write().proxy = given_proxy;
        }
        Ok(())
    }
    /// Create proxy to BatterySimulator to perform changes
    pub fn create_proxy(&self) -> Result<spower::BatterySimulatorProxy, Error> {
        match connect_to_service::<spower::BatterySimulatorMarker>() {
            Ok(service) => Ok(service),
            Err(err) => fx_err_and_bail!(
                &with_line!(TAG),
                format_err!("Failed to create battery simulator proxy: {:?}", err)
            ),
        }
    }

    /// Checks if the Facade proxy has been initialized
    pub fn check_proxy(&self) -> Result<(), Error> {
        if self.inner.read().proxy.as_ref().is_none() {
            bail!("Facade proxy has not been initialized");
        }
        Ok(())
    }

    /// Set Time Remaining to given value represented in seconds
    /// # Arguments
    /// * 'time_remaining' - A json object with 'message' as the key and an integer as a value
    /// representing time in seconds.
    // TODO(fxbug.dev/48702): Check type conversion
    pub fn set_time_remaining(&self, time_remaining: Value) -> Result<(), Error> {
        self.check_proxy()?;
        let seconds: u64 = match time_remaining["message"].as_u64() {
            Some(v) => v,
            None => bail!("Unable to get seconds"),
        };
        let microseconds = 0;
        let duration = Duration::new(seconds, microseconds);
        match self.inner.read().proxy.clone() {
            Some(p) => p.set_time_remaining(duration.as_nanos() as i64)?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Set Battery Status to given value
    /// # Arguments
    /// * 'battery_status' - A json object with 'message' as the key and an string as a value
    /// representing the battery status value
    pub fn set_battery_status(&self, battery_status: Value) -> Result<(), Error> {
        self.check_proxy()?;
        let value: &str = match battery_status["message"].as_str() {
            Some(v) => &v,
            None => bail!("Unable to get battery status"),
        };
        let res = match value {
            "UNKNOWN" => fpower::BatteryStatus::Unknown,
            "OK" => fpower::BatteryStatus::Ok,
            "NOT_AVAILABLE" => fpower::BatteryStatus::NotAvailable,
            "NOT_PRESENT" => fpower::BatteryStatus::NotPresent,
            _ => fx_err_and_bail!(&with_line!(TAG), format_err!("Battery Status not valid")),
        };
        match self.inner.read().proxy.clone() {
            Some(p) => p.set_battery_status(res)?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Set Charge Status to given value represented as a string
    /// # Arguments
    /// * 'charge_status' - A json object with 'message' as the key and an string as a value
    /// representing the charge status value
    pub fn set_charge_status(&self, charge_status: Value) -> Result<(), Error> {
        self.check_proxy()?;
        let value: &str = match charge_status["message"].as_str() {
            Some(v) => &v,
            None => bail!("Unable to get charge status"),
        };
        let res = match value {
            "UNKNOWN" => fpower::ChargeStatus::Unknown,
            "NOT_CHARGING" => fpower::ChargeStatus::NotCharging,
            "CHARGING" => fpower::ChargeStatus::Charging,
            "DISCHARGING" => fpower::ChargeStatus::Discharging,
            "FULL" => fpower::ChargeStatus::Full,
            _ => fx_err_and_bail!(&with_line!(TAG), format_err!("Charge Status not valid")),
        };
        match self.inner.read().proxy.clone() {
            Some(p) => p.set_charge_status(res)?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Set Level Status to given value represented as a string
    /// # Arguments
    /// * 'level_status' - A json object with 'message' as the key and an string as a value
    /// representing the level status value
    pub fn set_level_status(&self, level_status: Value) -> Result<(), Error> {
        self.check_proxy()?;
        let value: &str = match level_status["message"].as_str() {
            Some(v) => &v,
            None => bail!("Unable to get level status"),
        };
        let res = match value {
            "UNKNOWN" => fpower::LevelStatus::Unknown,
            "OK" => fpower::LevelStatus::Ok,
            "WARNING" => fpower::LevelStatus::Warning,
            "LOW" => fpower::LevelStatus::Low,
            "CRITICAL" => fpower::LevelStatus::Critical,
            _ => fx_err_and_bail!(&with_line!(TAG), format_err!("Level Status not valid")),
        };
        match self.inner.read().proxy.clone() {
            Some(p) => p.set_level_status(res)?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Set Charge Source to given value represented
    /// # Arguments
    /// * 'charge_source' - A json object with 'message' as the key and an string as a value
    /// representing the charge source value
    pub fn set_charge_source(&self, charge_source: Value) -> Result<(), Error> {
        self.check_proxy()?;
        let value: &str = match &charge_source["message"].as_str() {
            Some(v) => &v,
            None => bail!("Unable to get charge source"),
        };
        let res = match value {
            "UNKNOWN" => fpower::ChargeSource::Unknown,
            "NONE" => fpower::ChargeSource::None,
            "AC_ADAPTER" => fpower::ChargeSource::AcAdapter,
            "USB" => fpower::ChargeSource::Usb,
            "WIRELESS" => fpower::ChargeSource::Wireless,
            _ => fx_err_and_bail!(&with_line!(TAG), format_err!("Charge Source not valid")),
        };
        match self.inner.read().proxy.clone() {
            Some(p) => p.set_charge_source(res)?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Set Battery Percentage to given value
    /// # Arguments
    /// * 'battery_percentage' - A json object with 'message' as the key and an integer as a value
    /// representing the battery percentage
    pub fn set_battery_percentage(&self, battery_percentage: Value) -> Result<(), Error> {
        self.check_proxy()?;
        let percent: f32 = match battery_percentage["message"].to_string().parse() {
            Ok(v) => v,
            Err(e) => bail!("Unable to get battery percentage {}", e),
        };
        let battery_percentage_lower_bound = 0.0;
        let battery_percentage_upper_bound = 100.0;
        if percent < battery_percentage_lower_bound || percent > battery_percentage_upper_bound {
            fx_err_and_bail!(
                &with_line!(TAG),
                format_err!("Battery Percentage not between 0 and 100")
            )
        }
        match self.inner.read().proxy.clone() {
            Some(p) => p.set_battery_percentage(percent)?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Disconnect Real Battery
    pub fn disconnect_real_battery(&self) -> Result<(), Error> {
        self.check_proxy()?;
        match self.inner.read().proxy.clone() {
            Some(p) => p.disconnect_real_battery()?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Reconnect Real Battery
    pub fn reconnect_real_battery(&self) -> Result<(), Error> {
        self.check_proxy()?;
        match self.inner.read().proxy.clone() {
            Some(p) => p.reconnect_real_battery()?,
            None => bail!("Proxy not set"),
        };
        Ok(())
    }

    /// Returns the simulated battery info
    pub async fn get_simulated_battery_info(&self) -> Result<Option<fpower::BatteryInfo>, Error> {
        self.check_proxy()?;
        match self.inner.read().proxy.clone() {
            Some(p) => match p.get_battery_info().await {
                Ok(battery_info) => Ok(Some(battery_info)),
                Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. }) => {
                    fx_log_warn!("Battery Simulator not available.");
                    Ok(None)
                }
                Err(e) => fx_err_and_bail!(
                    &with_line!(TAG),
                    format_err!("Couldn't get BatteryInfo {}", e)
                ),
            },
            None => bail!("Proxy not set"),
        }
    }

    /// Returns a boolean value indicating if the device is simulating the battery state
    pub async fn get_simulating_state(&self) -> Result<Option<bool>, Error> {
        self.check_proxy()?;
        match self.inner.read().proxy.clone() {
            Some(p) => match p.is_simulating().await {
                Ok(simulation_state) => Ok(Some(simulation_state)),
                Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. }) => {
                    fx_log_warn!("Battery Simulator not available.");
                    Ok(None)
                }
                Err(e) => fx_err_and_bail!(
                    &with_line!(TAG),
                    format_err!("Couldn't get simulation state {}", e)
                ),
            },
            None => bail!("Proxy not set"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_component::client::{launch, launcher};
    use serde_json::json;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_disconnect() {
        // Launch battery manager
        const BM_URL: &str = "fuchsia-pkg://fuchsia.com/battery-manager#meta/battery_manager.cmx";
        let launcher = launcher().unwrap();
        let app = launch(&launcher, BM_URL.to_string(), None).unwrap();
        // Get Proxy and create Facade
        let proxy = app.connect_to_service::<spower::BatterySimulatorMarker>().unwrap();
        let facade = BatterySimulatorFacade::new();
        let init_result = facade.init_proxy(Some(proxy));
        assert!(init_result.is_ok(), "Failed to initialize proxy");
        // Disconnect
        let res = facade.disconnect_real_battery();
        assert!(res.is_ok(), "Failed to disconnect");
        // Check if simulating
        let simulation_state = facade.get_simulating_state().await;
        // When getting the state back, note that the DUT may not include
        // battery support, so it may be empty. This is not a test failure.
        match simulation_state.unwrap() {
            Some(state) => {
                assert_eq!(state, true);
                // Reconnect real battery
                let res = facade.reconnect_real_battery();
                assert!(res.is_ok(), "Failed to reconnect");
            }
            None => fx_log_warn!("No battery state provided, skipping check"),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_set_battery_percentage() {
        // Launch battery manager
        const BM_URL: &str = "fuchsia-pkg://fuchsia.com/battery-manager#meta/battery_manager.cmx";
        let launcher = launcher().unwrap();
        let app = launch(&launcher, BM_URL.to_string(), None).unwrap();
        // Get Proxy and create Facade
        let proxy = app.connect_to_service::<spower::BatterySimulatorMarker>().unwrap();
        let facade = BatterySimulatorFacade::new();
        let init_result = facade.init_proxy(Some(proxy));
        assert!(init_result.is_ok(), "Failed to initialize proxy");
        // Disconnect
        let res = facade.disconnect_real_battery();
        assert!(res.is_ok(), "Failed to disconnect");
        // Set Battery Percentage
        let res = facade.set_battery_percentage(json!({"message": 12}));
        assert!(res.is_ok(), "Failed to set battery percentage");
        // Get BatteryInfo
        let battery_info = facade.get_simulated_battery_info().await;
        assert!(battery_info.is_ok(), "Failed to get battery info");
        // When getting the battery info back, note that the DUT may not include
        // battery support, so info may be empty. This is not a test failure.
        match battery_info.unwrap() {
            Some(info) => {
                assert_eq!(info.level_percent.unwrap(), 12.0);
                // Reconnect real battery
                let res = facade.reconnect_real_battery();
                assert!(res.is_ok(), "Failed to reconnect");
            }
            None => fx_log_warn!("No battery info provided, skipping check"),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_set_charge_source() {
        // Launch battery manager
        const BM_URL: &str = "fuchsia-pkg://fuchsia.com/battery-manager#meta/battery_manager.cmx";
        let launcher = launcher().unwrap();
        let app = launch(&launcher, BM_URL.to_string(), None).unwrap();
        // Get Proxy and create Facade
        let proxy = app.connect_to_service::<spower::BatterySimulatorMarker>().unwrap();
        let facade = BatterySimulatorFacade::new();
        let init_result = facade.init_proxy(Some(proxy));
        assert!(init_result.is_ok(), "Failed to initialize proxy");
        // Disconnect
        let res = facade.disconnect_real_battery();
        assert!(res.is_ok(), "Failed to disconnect");
        // Set Charge Source
        let res = facade.set_charge_source(json!({"message": "WIRELESS"}));
        assert!(res.is_ok(), "Failed to set battery percentage");
        // Get BatteryInfo
        let battery_info = facade.get_simulated_battery_info().await;
        assert!(battery_info.is_ok(), "Failed to get battery info");
        match battery_info.unwrap() {
            Some(info) => {
                assert_eq!(info.charge_source.unwrap(), fpower::ChargeSource::Wireless);
                // Reconnect real battery
                let res = facade.reconnect_real_battery();
                assert!(res.is_ok(), "Failed to reconnect");
            }
            None => fx_log_warn!("No battery info provided, skipping check"),
        }
    }
}
