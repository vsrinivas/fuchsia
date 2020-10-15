// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};

use serde_json::{from_value, to_value, Value};

use crate::setui::types::{IntlInfo, NetworkType, SetUiResult};
use fidl_fuchsia_settings::{ConfigurationInterfaces, IntlMarker, SetupMarker, SetupSettings};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::macros::fx_log_info;

/// Facade providing access to SetUi interfaces.
#[derive(Debug)]
pub struct SetUiFacade {}

impl SetUiFacade {
    pub fn new() -> SetUiFacade {
        SetUiFacade {}
    }

    /// Sets network option used by device setup.
    /// Same action as choosing "Setup over Ethernet [enabled|disabled]" in "Developer options"
    ///
    /// args: accepted args are "ethernet" or "wifi". ex: {"params": "ethernet"}
    pub async fn set_network(&self, args: Value) -> Result<Value, Error> {
        let network_type: NetworkType = from_value(args)?;
        fx_log_info!("set_network input {:?}", network_type);
        let setup_service_proxy = match connect_to_service::<SetupMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Setup service {:?}.", e),
        };

        let mut settings = SetupSettings::empty();

        match network_type {
            NetworkType::Ethernet => {
                settings.enabled_configuration_interfaces = Some(ConfigurationInterfaces::Ethernet);
            }
            NetworkType::Wifi => {
                settings.enabled_configuration_interfaces = Some(ConfigurationInterfaces::Wifi);
            }
            _ => return Err(format_err!("Network type must either be ethernet or wifi.")),
        }
        // Update network configuration without automatic device reboot.
        // For changes to take effect, either restart basemgr component or reboot device.
        match setup_service_proxy.set2(settings, false).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(err) => Err(format_err!("Update network settings failed with err {:?}", err)),
        }
    }

    /// Reports the network option used for setup
    ///
    /// Returns either "ethernet", "wifi" or "unknown".
    pub async fn get_network_setting(&self) -> Result<Value, Error> {
        let setup_service_proxy = match connect_to_service::<SetupMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Setup service {:?}.", e),
        };
        let setting = setup_service_proxy.watch().await?;
        match setting.enabled_configuration_interfaces {
            Some(ConfigurationInterfaces::Ethernet) => Ok(to_value(NetworkType::Ethernet)?),
            Some(ConfigurationInterfaces::Wifi) => Ok(to_value(NetworkType::Wifi)?),
            _ => Ok(to_value(NetworkType::Unknown)?),
        }
    }

    /// Sets Internationalization values.
    ///
    /// Input should is expected to be type IntlInfo in json.
    /// Example:
    /// {
    ///     "hour_cycle":"H12",
    ///     "locales":[{"id":"he-FR"}],
    ///     "temperature_unit":"Celsius",
    ///     "time_zone_id":"UTC"
    /// }
    pub async fn set_intl_setting(&self, args: Value) -> Result<Value, Error> {
        let intl_info: IntlInfo = from_value(args)?;
        fx_log_info!("Received Intl Settings Request {:?}", intl_info);

        let intl_service_proxy = match connect_to_service::<IntlMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Intl service {:?}.", e),
        };
        match intl_service_proxy.set(intl_info.into()).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(err) => Err(format_err!("Update intl settings failed with err {:?}", err)),
        }
    }

    /// Reads the Internationalization setting.
    ///
    /// Returns IntlInfo in json.
    pub async fn get_intl_setting(&self) -> Result<Value, Error> {
        let intl_service_proxy = match connect_to_service::<IntlMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Intl service {:?}.", e),
        };
        let intl_info: IntlInfo = intl_service_proxy.watch().await?.into();
        return Ok(to_value(&intl_info)?);
    }
}

#[cfg(test)]
mod tests {
    use crate::common_utils::test::assert_value_round_trips_as;
    use crate::setui::types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};
    use serde_json::json;

    fn make_intl_info() -> IntlInfo {
        return IntlInfo {
            locales: Some(vec![LocaleId { id: "en-US".into() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
            time_zone_id: Some("UTC".into()),
            hour_cycle: Some(HourCycle::H12),
        };
    }

    #[test]
    fn serde_intl_set() {
        let intl_request = make_intl_info();
        assert_value_round_trips_as(
            intl_request,
            json!(
            {
                "locales": [{"id": "en-US"}],
                "temperature_unit":"Celsius",
                "time_zone_id": "UTC",
                "hour_cycle": "H12",
            }),
        );
    }
}
