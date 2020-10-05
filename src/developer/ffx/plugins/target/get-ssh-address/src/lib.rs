// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_core::ffx_plugin,
    ffx_daemon::target::{SshFormatter, TargetAddr},
    ffx_get_ssh_address_args::GetSshAddressCommand,
    fidl_fuchsia_developer_bridge as bridge,
    std::io::{stdout, Write},
    std::time::Duration,
};

#[ffx_plugin()]
pub async fn get_ssh_address(
    daemon_proxy: bridge::DaemonProxy,
    cmd: GetSshAddressCommand,
) -> Result<()> {
    get_ssh_address_impl(daemon_proxy, cmd, Box::new(stdout())).await
}

async fn get_ssh_address_impl<W: Write>(
    daemon_proxy: bridge::DaemonProxy,
    cmd: GetSshAddressCommand,
    mut writer: W,
) -> Result<()> {
    let timeout = Duration::from_nanos((cmd.timeout.unwrap_or(1.0) * 1000000000.0) as u64);
    let res = daemon_proxy
        .get_ssh_address(cmd.nodename.as_ref().unwrap_or(&"".to_owned()), timeout.as_nanos() as i64)
        .await?
        .map_err(|e| anyhow!("getting ssh addr: {:?}", e))?;
    let out = TargetAddr::from(res);
    out.ssh_fmt(&mut writer)?;
    writeln!(writer, "")?;
    Ok(())
}
