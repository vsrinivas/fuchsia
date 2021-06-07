// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_serve_args::ServeCommand,
    fidl_fuchsia_developer_bridge::RepositoriesProxy,
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    std::net,
};

#[ffx_plugin(RepositoriesProxy = "daemon::service")]
pub async fn serve(cmd: ServeCommand, repo: RepositoriesProxy) -> Result<()> {
    let mut ip = match cmd.listen_address.ip() {
        net::IpAddr::V4(addr) => IpAddress::Ipv4(Ipv4Address { addr: addr.octets() }),
        net::IpAddr::V6(addr) => IpAddress::Ipv6(Ipv6Address { addr: addr.octets() }),
    };

    if !repo.serve(&mut ip, cmd.listen_address.port()).await? {
        ffx_bail!("Could not start server. Check the daemon log for details.")
    }

    Ok(())
}
