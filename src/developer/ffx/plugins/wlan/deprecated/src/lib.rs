// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_wlan_deprecated_args as arg_types,
    fidl_fuchsia_wlan_product_deprecatedconfiguration as wlan_deprecated,
};

#[ffx_plugin(
    wlan_deprecated::DeprecatedConfiguratorProxy = "core/wlancfg:expose:fuchsia.wlan.product.deprecatedconfiguration.DeprecatedConfigurator"
)]
pub async fn handle_deprecated_command(
    proxy: wlan_deprecated::DeprecatedConfiguratorProxy,
    cmd: arg_types::DeprecatedCommand,
) -> Result<(), Error> {
    match cmd.subcommand {
        arg_types::DeprecatedSubcommand::SuggestMac(mac) => {
            donut_lib::handle_suggest_ap_mac(proxy, mac.mac).await
        }
    }
}
