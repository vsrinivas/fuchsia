// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_get_ssh_address_args::GetSshAddressCommand,
    fidl_fuchsia_developer_bridge::{DaemonError, DaemonProxy, TargetAddrInfo},
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    netext::scope_id_to_name,
    std::io::{stdout, Write},
    std::net::IpAddr,
    std::time::Duration,
};

// This constant can be removed, and the implementation can assert that a port
// always comes from the daemon after some transition period (~May '21).
const DEFAULT_SSH_PORT: u16 = 22;

#[ffx_plugin()]
pub async fn get_ssh_address(daemon_proxy: DaemonProxy, cmd: GetSshAddressCommand) -> Result<()> {
    get_ssh_address_impl(daemon_proxy, cmd, Box::new(stdout())).await
}

async fn get_ssh_address_impl<W: Write>(
    daemon_proxy: DaemonProxy,
    cmd: GetSshAddressCommand,
    mut writer: W,
) -> Result<()> {
    let timeout = Duration::from_nanos((cmd.timeout().await? * 1000000000.0) as u64);
    let target: Option<String> = ffx_config::get("target.default").await?;
    let res = daemon_proxy
        .get_ssh_address(target.as_deref(), timeout.as_nanos() as i64)
        .await?
        .or_else(|e| match e {
            DaemonError::TargetInZedboot => ffx_bail!("Targets in zedboot do not support ssh"),
            DaemonError::TargetInFastboot => ffx_bail!("Targets in fastboot do not support ssh"),
            _ => ffx_bail!(
                "Failed to get SSH address of {}: {:?}",
                target.unwrap_or("a unique target".to_string()),
                e
            ),
        })?;

    let (ip, scope, port) = match res {
        TargetAddrInfo::Ip(info) => {
            let ip = match info.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => IpAddr::from(addr),
                IpAddress::Ipv4(Ipv4Address { addr }) => IpAddr::from(addr),
            };
            (ip, info.scope_id, 0)
        }
        TargetAddrInfo::IpPort(info) => {
            let ip = match info.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => IpAddr::from(addr),
                IpAddress::Ipv4(Ipv4Address { addr }) => IpAddr::from(addr),
            };
            (ip, info.scope_id, info.port)
        }
    };

    match ip {
        IpAddr::V4(ip) => {
            write!(writer, "{}", ip)?;
        }
        IpAddr::V6(ip) => {
            write!(writer, "[{}", ip)?;
            if scope > 0 {
                write!(writer, "%{}", scope_id_to_name(scope))?;
            }
            write!(writer, "]")?;
        }
    }
    write!(writer, ":{}", if port == 0 { DEFAULT_SSH_PORT } else { port })?;
    writeln!(writer)?;

    Ok(())
}
