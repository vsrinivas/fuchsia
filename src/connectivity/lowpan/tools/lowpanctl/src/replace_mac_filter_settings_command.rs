// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use fidl_fuchsia_lowpan_test::*;

/// Contains the arguments decoded for the `replace-mac-filter-settings` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "replace-mac-filter-settings")]
pub struct ReplaceMacFilterSettingsCommand {
    #[argh(option, description = "only \"disabled\", \"allow\" and \"deny\" are vaild inputs.")]
    pub mode: Option<String>,

    #[argh(
        option,
        description = "each mac addr filter item contains mac addresses as hex strings, \
        rssi as decimal and seperate by dot. Mac addr filter items are seperated by comma, \
        i.e. 7AAD966C55768C64.-10,7AAD966C55768C65.-20 or 7AAD966C55768C64,7AAD966C55768C65"
    )]
    pub mac_addr_filter_items: Option<String>,
}

impl ReplaceMacFilterSettingsCommand {
    fn get_mac_addr_vec_from_str(&self, mac_addr_str: &str) -> Result<Vec<u8>, Error> {
        let res = hex::decode(mac_addr_str.to_string())?.to_vec();
        if res.len() != 8 {
            return Err(format_err!("mac addr has to be EUI 64 type (8 bytes)"));
        }
        Ok(res)
    }

    fn get_mac_addr_filter_item_vec(&self) -> Result<Vec<MacAddressFilterItem>, Error> {
        let mut filter_item_vec = Vec::<MacAddressFilterItem>::new();
        let mac_addr_filter_item_str_iter = self.mac_addr_filter_items.as_ref().unwrap().split(',');
        for mac_addr_filter_item_str in mac_addr_filter_item_str_iter {
            let str_vec: Vec<&str> = mac_addr_filter_item_str.split('.').collect();
            let mac_addr = self.get_mac_addr_vec_from_str(str_vec[0])?;
            let mut rssi_option = None;
            if str_vec.len() > 1 {
                rssi_option = Some(str_vec[1].parse()?);
            }
            filter_item_vec.push(MacAddressFilterItem {
                mac_address: Some(mac_addr),
                rssi: rssi_option,
                ..MacAddressFilterItem::EMPTY
            });
        }
        Ok(filter_item_vec)
    }

    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let (_, _, device_test_proxy) =
            context.get_default_device_proxies().await.context("Unable to get device instance")?;

        let mode = match self.mode.as_ref().unwrap().to_lowercase().as_str() {
            "allow" => MacAddressFilterMode::Allow,
            "deny" => MacAddressFilterMode::Deny,
            "disabled" => MacAddressFilterMode::Disabled,
            _ => {
                assert!(false);
                MacAddressFilterMode::Disabled
            }
        };

        if mode == MacAddressFilterMode::Disabled {
            device_test_proxy
                .replace_mac_address_filter_settings(MacAddressFilterSettings {
                    mode: Some(mode),
                    items: None,
                    ..MacAddressFilterSettings::EMPTY
                })
                .await?;
        } else {
            let mac_addr_filter_item_vec = self.get_mac_addr_filter_item_vec()?;
            device_test_proxy
                .replace_mac_address_filter_settings(MacAddressFilterSettings {
                    mode: Some(mode),
                    items: Some(mac_addr_filter_item_vec),
                    ..MacAddressFilterSettings::EMPTY
                })
                .await?;
        }
        Ok(())
    }
}
