// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{server::Facade, wlan_deprecated::facade::WlanDeprecatedConfigurationFacade},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fuchsia_syslog::macros::*,
    serde_json::{to_value, Value},
};

#[async_trait(?Send)]
impl Facade for WlanDeprecatedConfigurationFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "suggest_ap_mac" => {
                let mac = self.parse_mac_argument(args)?;
                fx_log_info!(
                    tag: "WlanDeprecatedConfigurationFacade", "setting suggested MAC to: {:?}",
                    mac
                );
                let result = self.suggest_access_point_mac_address(mac).await?;
                to_value(result).map_err(|e| {
                    format_err!("error parsing suggested access point MAC result: {}", e)
                })
            }
            _ => return Err(format_err!("unsupported command!")),
        }
    }
}
