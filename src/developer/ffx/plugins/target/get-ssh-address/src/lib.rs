// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    bridge::TargetAddrInfo,
    ffx_core::ffx_plugin,
    ffx_daemon_core::net::scope_id_to_name,
    ffx_get_ssh_address_args::GetSshAddressCommand,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    std::io::{stdout, Write},
    std::net::IpAddr,
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
    let timeout = Duration::from_nanos((cmd.timeout().await? * 1000000000.0) as u64);
    let target: Option<ffx_config::Value> = ffx_config::get("target.default").await.ok();
    let res = daemon_proxy
        .get_ssh_address(target.as_ref().map(|s| s.as_str()).flatten(), timeout.as_nanos() as i64)
        .await?
        .map_err(|e| anyhow!("getting ssh addr: {:?}", e))?;

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
    if port > 0 {
        write!(writer, ":{}", port)?;
    }
    writeln!(writer)?;

    Ok(())
}
