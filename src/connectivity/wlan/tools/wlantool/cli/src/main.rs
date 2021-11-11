// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_wlan_device_service::{DeviceMonitorMarker, DeviceServiceMarker},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    structopt::StructOpt,
    wlan_dev,
};

fn main() -> Result<(), Error> {
    println!(
        "Warning: this tool may cause state mismatches between layers of the WLAN \n\
        subsystem. It is intended for use by WLAN developers only. Please reach out \n\
        to the WLAN team if your use case relies on it."
    );
    let opt = wlan_dev::opts::Opt::from_args();
    println!("{:?}", opt);

    let mut exec = fasync::LocalExecutor::new().context("error creating event loop")?;
    let dev_svc_proxy = connect_to_protocol::<DeviceServiceMarker>()
        .context("failed to `connect` to device service")?;
    let monitor_proxy = connect_to_protocol::<DeviceMonitorMarker>()
        .context("failed to `connect` to device monitor")?;

    let fut = wlan_dev::handle_wlantool_command(dev_svc_proxy, monitor_proxy, opt);
    exec.run_singlethreaded(fut)
}
