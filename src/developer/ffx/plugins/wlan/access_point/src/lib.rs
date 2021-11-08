// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, donut_lib, ffx_core::ffx_plugin, ffx_wlan_ap_args as arg_types, ffx_wlan_common,
    fidl_fuchsia_wlan_policy as wlan_policy,
};

#[ffx_plugin(
    wlan_policy::AccessPointProviderProxy = "core/wlancfg:expose:fuchsia.wlan.policy.AccessPointProvider",
    wlan_policy::AccessPointListenerProxy = "core/wlancfg:expose:fuchsia.wlan.policy.AccessPointListener"
)]
pub async fn handle_client_command(
    ap_provider: wlan_policy::AccessPointProviderProxy,
    ap_listener: wlan_policy::AccessPointListenerProxy,
    cmd: arg_types::ApCommand,
) -> Result<(), Error> {
    let (ap_controller, _) = ffx_wlan_common::get_ap_controller(ap_provider).await?;
    let listener_stream = ffx_wlan_common::get_ap_listener_stream(ap_listener)?;

    match cmd.subcommand {
        arg_types::ApSubcommand::Listen(arg_types::Listen {}) => {
            donut_lib::handle_ap_listen(listener_stream).await
        }
        arg_types::ApSubcommand::Start(config) => {
            let config = wlan_policy::NetworkConfig::from(config);
            donut_lib::handle_start_ap(ap_controller, listener_stream, config).await
        }
        arg_types::ApSubcommand::Stop(config) => {
            let config = wlan_policy::NetworkConfig::from(config);
            donut_lib::handle_stop_ap(ap_controller, config).await
        }
        arg_types::ApSubcommand::StopAll(arg_types::StopAll {}) => {
            donut_lib::handle_stop_all_aps(ap_controller).await
        }
    }
}
