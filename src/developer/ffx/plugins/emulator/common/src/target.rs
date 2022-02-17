// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fidl_fuchsia_developer_bridge as bridge;
use fidl_fuchsia_net as net;
use std::time::Duration;
use timeout::timeout;

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

/// Equivalent to a call to `ffx target remove`. This removes the emulator with the name which
/// matches the target_id parameter from the target list. If no such target exists, the function
/// logs a warning.
pub async fn remove_target(proxy: &bridge::TargetCollectionProxy, target_id: &str) -> Result<()> {
    if proxy.remove_target(target_id).await? {
        log::debug!("[emulator] Removed target {:?}", target_id);
    } else {
        log::warn!("[emulator] No matching target found for {:?}", target_id);
    }
    Ok(())
}

/// Makes a call to the TargetCollection (similar to `ffx target show`) to see if it has a target
/// that matches the specified emulator. If the emulator responds to the request, such that a
/// handle is returned, it's considered "active".
///
/// The request documentation indicates that the call to OpenTarget will hang until the device
/// responds, possibly indefinitely. We wrap the call in a timeout of 1 second, so this function
/// will not hang indefinitely. If the caller expects the response to take longer (such as during
/// Fuchsia bootup), it's safe to call the function repeatedly with a longer local timeout.
pub async fn is_active(collection_proxy: &bridge::TargetCollectionProxy, name: &str) -> bool {
    let (_proxy, handle) = fidl::endpoints::create_proxy::<bridge::TargetMarker>().unwrap();
    let target = Some(name.to_string());
    let res = timeout(Duration::from_secs(1), async {
        collection_proxy
            .open_target(
                bridge::TargetQuery { string_matcher: target, ..bridge::TargetQuery::EMPTY },
                handle,
            )
            .await
    })
    .await;
    return res.is_ok()                    // It didn't time out...
        && res.as_ref().unwrap().is_ok()  // The call was issued successfully...
        && res.unwrap().unwrap().is_ok(); // And the actual return value was Ok(_).
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

    fn setup_fake_target_server_open<T: 'static + Fn(String) -> bool + Send>(
        test: T,
    ) -> TargetCollectionProxy {
        setup_fake_target_proxy(move |req| match req {
            TargetCollectionRequest::OpenTarget { query, responder, .. } => {
                assert!(query.string_matcher.is_some());
                if !test(query.string_matcher.unwrap()) {
                    assert!(responder
                        .send(&mut Err(bridge::OpenTargetError::TargetNotFound))
                        .is_ok());
                } else {
                    assert!(responder.send(&mut Ok(())).is_ok());
                }
            }
            _ => assert!(false),
        })
    }

    fn setup_fake_target_server_open_timeout() -> TargetCollectionProxy {
        setup_fake_target_proxy(move |req| match req {
            TargetCollectionRequest::OpenTarget { .. } => {
                // We just don't send a response. This thread terminates right away, but the caller
                // will still timeout waiting for a response that never comes.
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove() {
        let server = setup_fake_target_server_remove(|id| assert_eq!(id, "target_id".to_owned()));
        remove_target(&server, "target_id").await.unwrap();
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_is_active() {
        let server = setup_fake_target_server_open(|id| id == "target_id".to_owned());
        // The "target" that we expect is "active".
        assert!(is_active(&server, "target_id").await);
        // The "target" that we don't expect is "inactive".
        assert!(!is_active(&server, "bad_target").await);

        // This should timeout waiting for a response, which is indicated as "inactive".
        let timeout_server = setup_fake_target_server_open_timeout();
        assert!(!is_active(&timeout_server, "target").await);
    }
}
