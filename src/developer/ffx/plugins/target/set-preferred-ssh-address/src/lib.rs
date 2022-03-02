// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    ffx_core::ffx_plugin,
    ffx_target_set_preferred_ssh_address_args::SetPreferredSshAddressCommand,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_net as fnet,
};

#[ffx_plugin()]
pub async fn set_preferred_ssh_address(
    target_proxy: bridge::TargetProxy,
    cmd: SetPreferredSshAddressCommand,
) -> Result<()> {
    let (addr, scope, _port) = netext::parse_address_parts(cmd.addr.as_str())
        .map_err(|e| errors::ffx_error!("parse_address_parts failed: {:?}", e))?;

    let scope_id = if let Some(scope) = scope {
        scope.parse::<u32>().unwrap_or_else(|_| netext::name_to_scope_id(scope))
    } else {
        0
    };

    let ip = match addr {
        std::net::IpAddr::V6(i) => {
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: i.octets().into() })
        }
        std::net::IpAddr::V4(i) => {
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: i.octets().into() })
        }
    };

    target_proxy
        .set_preferred_ssh_address(&mut bridge::TargetIp { ip, scope_id })
        .await
        .context("set_preferred_ssh_address failed")?
        .map_err(|e| anyhow::anyhow!("set_preferred_ssh_address error: {:?}", e))
}

#[cfg(test)]
mod tests {
    use super::*;
    use net_declare::fidl_ip;

    const IPV6_ADDRESS: fnet::IpAddress = fidl_ip!("fe80::1");
    const IPV4_ADDRESS: fnet::IpAddress = fidl_ip!("192.168.0.1");

    fn setup_fake_target_server(expected_ip: bridge::TargetIp) -> bridge::TargetProxy {
        setup_fake_target_proxy(move |req| match req {
            bridge::TargetRequest::SetPreferredSshAddress { ip, responder } => {
                assert_eq!(expected_ip, ip);
                responder.send(&mut Ok(())).expect("set_preferred_ssh_address failed");
            }
            r => panic!("got unexpected request: {:?}", r),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ipv4() {
        set_preferred_ssh_address(
            setup_fake_target_server(bridge::TargetIp { ip: IPV4_ADDRESS, scope_id: 0 }),
            SetPreferredSshAddressCommand { addr: "192.168.0.1".to_string() },
        )
        .await
        .expect("set_preferred_ssh_address error");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ipv6_with_no_scope() {
        set_preferred_ssh_address(
            setup_fake_target_server(bridge::TargetIp { ip: IPV6_ADDRESS, scope_id: 0 }),
            SetPreferredSshAddressCommand { addr: "fe80::1".to_string() },
        )
        .await
        .expect("set_preferred_ssh_address error");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ipv6_with_numeric_scope() {
        set_preferred_ssh_address(
            setup_fake_target_server(bridge::TargetIp { ip: IPV6_ADDRESS, scope_id: 1 }),
            SetPreferredSshAddressCommand { addr: "fe80::1%1".to_string() },
        )
        .await
        .expect("set_preferred_ssh_address error");
    }
}
