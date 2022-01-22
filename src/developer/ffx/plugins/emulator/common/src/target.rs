// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fidl_fuchsia_developer_bridge as bridge;
use fidl_fuchsia_net as net;

/// Equivalent to a call to `ffx target add`. This adds a target at `127.0.0.1:ssh_port`.
/// At this time, this is restricted to IPV4 only, as QEMU's DHCP server gets in the way of port
/// mapping on IPV6.
pub async fn add_target(proxy: &bridge::TargetCollectionProxy, ssh_port: u16) -> Result<()> {
    let mut addr = bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
        ip: net::IpAddress::Ipv4(net::Ipv4Address {
            addr: "127.0.0.1".parse::<std::net::Ipv4Addr>().unwrap().octets().into(),
        }),
        port: ssh_port,
        scope_id: 0,
    });

    proxy.add_target(&mut addr).await?;
    log::debug!("[emulator] Added target {:?}", &addr);
    Ok(())
}

/// Equivalent to a call to `ffx target remove`. This removes the emulator at
/// `127.0.0.1:ssh_port` from the target list. If no such target exists, the function logs a
/// warning.
pub async fn remove_target(proxy: &bridge::TargetCollectionProxy, ssh_port: u16) -> Result<()> {
    let target_id = format!("127.0.0.1:{}", ssh_port);
    if proxy.remove_target(&target_id).await? {
        log::debug!("[emulator] Removed target {:?}", target_id);
    } else {
        log::warn!("[emulator] No matching target found for {:?}", target_id);
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge::{TargetCollectionProxy, TargetCollectionRequest};
    use fuchsia_async::futures::TryStreamExt;

    fn setup_fake_target_proxy<R: 'static>(mut handle_request: R) -> TargetCollectionProxy
    where
        R: FnMut(
            fidl::endpoints::Request<<TargetCollectionProxy as fidl::endpoints::Proxy>::Protocol>,
        ),
    {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<
            <TargetCollectionProxy as fidl::endpoints::Proxy>::Protocol,
        >()
        .unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                handle_request(req);
            }
        })
        .detach();
        proxy
    }

    fn setup_fake_target_server_add<T: 'static + Fn(bridge::TargetAddrInfo) + Send>(
        test: T,
    ) -> TargetCollectionProxy {
        setup_fake_target_proxy(move |req| match req {
            TargetCollectionRequest::AddTarget { ip, responder } => {
                test(ip);
                responder.send().unwrap();
            }
            _ => assert!(false),
        })
    }

    fn setup_fake_target_server_remove<T: 'static + Fn(String) + Send>(
        test: T,
    ) -> TargetCollectionProxy {
        setup_fake_target_proxy(move |req| match req {
            TargetCollectionRequest::RemoveTarget { target_id, responder } => {
                test(target_id);
                responder.send(true).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove() {
        let server =
            setup_fake_target_server_remove(|id| assert_eq!(id, "127.0.0.1:33399".to_owned()));
        remove_target(&server, 33399).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        let ssh_port = 12345;
        let server = setup_fake_target_server_add(move |addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
                    ip: net::IpAddress::Ipv4(net::Ipv4Address {
                        addr: "127.0.0.1".parse::<std::net::Ipv4Addr>().unwrap().octets().into(),
                    }),
                    port: ssh_port,
                    scope_id: 0,
                }),
            )
        });
        add_target(&server, ssh_port).await.unwrap();
    }
}
