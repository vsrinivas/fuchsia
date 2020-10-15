// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    eui48, fidl_fuchsia_net, fidl_fuchsia_wlan_product_deprecatedconfiguration as fidl_deprecated,
    fuchsia_component::client::connect_to_service,
    parking_lot::RwLock,
    std::fmt::Debug,
};

#[derive(Debug)]
struct InnerWlanDeprecatedConfigurationFacade {
    controller: fidl_deprecated::DeprecatedConfiguratorProxy,
}

#[derive(Debug)]
pub struct WlanDeprecatedConfigurationFacade {
    inner: RwLock<InnerWlanDeprecatedConfigurationFacade>,
}

impl WlanDeprecatedConfigurationFacade {
    /// Connects to the DeprecatedConfigurator service and returns a
    /// WlanDeprecatedConfigurationFacade instance to enable setting suggested MAC addresses for
    /// soft APs.
    pub fn new() -> Result<WlanDeprecatedConfigurationFacade, Error> {
        let controller = connect_to_service::<fidl_deprecated::DeprecatedConfiguratorMarker>()?;
        Ok(Self {
            inner: RwLock::new(InnerWlanDeprecatedConfigurationFacade { controller: controller }),
        })
    }

    /// Communicates with the DeprecatedConfigurator service to set a preferred MAC address to be
    /// used for new soft APs.  This API waits for an acknowledgement from the
    /// DeprecatedConfigurator service indicating that the suggested MAC address has been set.
    ///
    /// # Arguments
    ///
    /// `mac` - A MAC address in the form of an eui48::MacAddress.
    pub async fn suggest_access_point_mac_address(
        &self,
        mac: eui48::MacAddress,
    ) -> Result<(), Error> {
        let inner_guard = self.inner.read();
        let controller = &inner_guard.controller;

        let mut mac = fidl_fuchsia_net::MacAddress { octets: mac.to_array() };
        let result = controller.suggest_access_point_mac_address(&mut mac).await?;
        result.map_err(|e| format_err!("could not set preferred MAC: {:?}", e))
    }

    /// Consumes the arguments from an ACTS test, looks for a "mac" key, and attempts to convert
    /// the associated value to a MAC address.
    ///
    /// # Arguments
    ///
    /// * `args` - A JSON blob represented as a serde_json::Value containing a "mac" key.
    pub fn parse_mac_argument(&self, args: serde_json::Value) -> Result<eui48::MacAddress, Error> {
        let mac = match args.get("mac") {
            Some(mac) => match mac.as_str() {
                Some(mac) => eui48::MacAddress::parse_str(mac)?,
                None => {
                    return Err(format_err!(
                        "MAC address could not be interpreted as a string: {:?}",
                        mac
                    ))
                }
            },
            None => return Err(format_err!("Please provide a preferred MAC")),
        };
        Ok(mac)
    }
}
