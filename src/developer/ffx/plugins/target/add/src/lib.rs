// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_core::ffx_plugin,
    ffx_target_add_args::AddCommand,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_net as net,
    std::net::IpAddr,
};

#[ffx_plugin()]
pub async fn add(daemon_proxy: bridge::DaemonProxy, cmd: AddCommand) -> Result<()> {
    let mut addr = bridge::TargetAddrInfo::Ip(bridge::TargetIp {
        ip: match cmd.addr {
            IpAddr::V6(i) => net::IpAddress::Ipv6(net::Ipv6Address { addr: i.octets().into() }),
            IpAddr::V4(i) => net::IpAddress::Ipv4(net::Ipv4Address { addr: i.octets().into() }),
        },

        // TODO: Add support for specifying scopes. This is best done at the same time as adding
        // DNS lookup, as the usual way to resolve a scope name to an ID is to just pass the whole
        // address to getaddrinfo(3).
        scope_id: 0,
    });

    if let Err(e) = daemon_proxy.add_target(&mut addr).await? {
        eprintln!("ERROR: {:?}", e);
        Err(anyhow!("Error adding target: {:?}", e))
    } else {
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge::DaemonRequest;

    fn setup_fake_daemon_server<T: 'static + Fn(bridge::TargetAddrInfo) + Send>(
        test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::AddTarget { ip, responder } => {
                test(ip);
                responder.send(&mut Ok(())).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        let server = setup_fake_daemon_server(|addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: net::IpAddress::Ipv4(net::Ipv4Address {
                        addr: "123.210.123.210"
                            .parse::<std::net::Ipv4Addr>()
                            .unwrap()
                            .octets()
                            .into()
                    }),
                    scope_id: 0,
                })
            )
        });
        add(server, AddCommand { addr: "123.210.123.210".parse().unwrap() }).await.unwrap();
    }
}
