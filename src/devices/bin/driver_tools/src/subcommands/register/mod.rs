// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common,
    anyhow::{format_err, Result},
    args::RegisterCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_driver_registrar as fdr,
    fidl_fuchsia_pkg as fpkg,
};

pub async fn register(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: RegisterCommand,
) -> Result<()> {
    let driver_registrar_proxy = common::get_registrar_proxy(remote_control, cmd.select).await?;
    register_impl(driver_registrar_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn register_impl<W: std::io::Write>(
    driver_registrar_proxy: fdr::DriverRegistrarProxy,
    cmd: RegisterCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(
        writer,
        "Notifying the driver manager that there might be a new version of {}",
        cmd.url
    )?;
    driver_registrar_proxy
        .register(&mut fpkg::PackageUrl { url: cmd.url.to_string() })
        .await?
        .map_err(|err| format_err!("{:?}", err))
}
