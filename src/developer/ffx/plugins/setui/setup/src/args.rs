// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_settings::ConfigurationInterfaces;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone, Copy)]
#[argh(subcommand, name = "setup")]
/// get or set setup settings
pub struct Setup {
    /// a supported group of interfaces, specified as a comma-delimited string of the valid values
    /// eth and wifi, e.g. "-i eth,wifi" or "-i wifi"
    #[argh(option, short = 'i', long = "interfaces", from_str_fn(str_to_interfaces))]
    pub configuration_interfaces: Option<ConfigurationInterfaces>,
}

fn str_to_interfaces(src: &str) -> Result<ConfigurationInterfaces, String> {
    src.to_lowercase().split(",").fold(Ok(ConfigurationInterfaces::empty()), |acc, flag| {
        acc.and_then(|acc| {
            Ok(match flag {
                "eth" | "ethernet" => ConfigurationInterfaces::ETHERNET,
                "wireless" | "wifi" => ConfigurationInterfaces::WIFI,
                bad_ifc => return Err(format!("Unknown interface: {:?}", bad_ifc)),
            } | acc)
        })
    })
}
