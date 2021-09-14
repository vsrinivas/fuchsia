// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::FfxError,
    ffx_core::ffx_plugin,
    ffx_get_ssh_address_args::GetSshAddressCommand,
    fidl_fuchsia_developer_bridge::{
        DaemonError, TargetAddrInfo, TargetCollectionProxy, TargetHandleMarker,
    },
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    netext::scope_id_to_name,
    std::io::{stdout, Write},
    std::net::IpAddr,
    std::time::Duration,
    timeout::timeout,
};

// This constant can be removed, and the implementation can assert that a port
// always comes from the daemon after some transition period (~May '21).
const DEFAULT_SSH_PORT: u16 = 22;

#[ffx_plugin(TargetCollectionProxy = "daemon::service")]
pub async fn get_ssh_address(
    collection_proxy: TargetCollectionProxy,
    cmd: GetSshAddressCommand,
) -> Result<()> {
    get_ssh_address_impl(collection_proxy, cmd, &mut stdout()).await
}

async fn get_ssh_address_impl<W: Write>(
    collection_proxy: TargetCollectionProxy,
    cmd: GetSshAddressCommand,
    writer: &mut W,
) -> Result<()> {
    let timeout_dur = Duration::from_secs_f64(cmd.timeout().await?);
    let (proxy, handle) = fidl::endpoints::create_proxy::<TargetHandleMarker>()?;
    let target: Option<String> = ffx_config::get("target.default").await?;
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let is_default_target = ffx.target.is_none();
    let t_clone = target.clone();
    let t_clone_2 = target.clone();
    let res = timeout(timeout_dur, async {
        collection_proxy.open_target(target.as_deref(), handle).await?.map_err(|err| {
            anyhow::Error::from(FfxError::OpenTargetError {
                err,
                target: t_clone_2,
                is_default_target,
            })
        })?;
        proxy.get_ssh_address().await.map_err(anyhow::Error::from)
    })
    .await
    .map_err(|_| FfxError::DaemonError {
        err: DaemonError::Timeout,
        target: t_clone,
        is_default_target,
    })??;
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
