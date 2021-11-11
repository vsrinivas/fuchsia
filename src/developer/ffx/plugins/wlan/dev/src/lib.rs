// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_wlan_dev_args as arg_types,
    fidl_fuchsia_wlan_device_service as wlan_service, wlan_dev,
};

#[ffx_plugin(
    wlan_service::DeviceServiceProxy = "core/wlanstack:expose:fuchsia.wlan.device.service.DeviceService",
    wlan_service::DeviceMonitorProxy = "core/wlandevicemonitor:expose:fuchsia.wlan.device.service.DeviceMonitor"
)]
pub async fn handle_dev_cmd(
    dev_svc_proxy: wlan_service::DeviceServiceProxy,
    monitor_proxy: wlan_service::DeviceMonitorProxy,
    cmd: arg_types::DevCommand,
) -> Result<(), Error> {
    wlan_dev::handle_wlantool_command(
        dev_svc_proxy,
        monitor_proxy,
        wlan_dev::opts::Opt::from(cmd.subcommand),
    )
    .await
}
