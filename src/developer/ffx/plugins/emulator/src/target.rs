// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    errors::ffx_bail,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_net as net,
    std::thread::sleep,
    std::time::Duration,
};

pub async fn add_target(daemon_proxy: &bridge::DaemonProxy, ssh_port: u16) -> Result<()> {
    let mut addr = bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
        ip: net::IpAddress::Ipv4(net::Ipv4Address {
            addr: "127.0.0.1".parse::<std::net::Ipv4Addr>().unwrap().octets().into(),
        }),
        port: ssh_port,
        scope_id: 0,
    });
    if let Err(e) = daemon_proxy.add_target(&mut addr).await? {
        ffx_bail!("Error adding target: {:?}", e)
    }
    Ok(())
}

pub async fn remove_target(
    daemon_proxy: &bridge::DaemonProxy,
    target_id: &mut String,
    retries: &mut i32,
) -> Result<()> {
    while *retries > 0 {
        match daemon_proxy.remove_target(target_id).await? {
            Ok(removed) => {
                if removed {
                    println!("[fvdl] removed target {:?}", target_id);
                } else {
                    println!("[fvdl] no matching target found for {:?}", target_id);
                }
                return Ok(());
            }
            Err(e) => {
                *retries -= 1;
                println!(
                    "[fvdl] WARN: Attempt to remove target failed with error {:?}. Retry left {}",
                    e, *retries
                );
                sleep(Duration::from_secs(1));
            }
        }
    }
    Err(format_err!("[fvdl] Error removing target"))
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_daemon_proxy;
    use fidl_fuchsia_developer_bridge::{DaemonError, DaemonRequest};

    fn setup_fake_daemon_server_add<T: 'static + Fn(bridge::TargetAddrInfo) + Send>(
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

    fn setup_fake_daemon_server_remove<T: 'static + Fn(String) + Send>(
        test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::RemoveTarget { target_id, responder } => {
                test(target_id);
                responder.send(&mut Ok(true)).unwrap();
            }
            DaemonRequest::Quit { responder } => {
                responder.send(true).unwrap();
            }
            _ => assert!(false),
        })
    }

    fn setup_fake_daemon_server_remove_with_err<T: 'static + Fn(String) + Send>(
        test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::RemoveTarget { target_id, responder } => {
                test(target_id);
                responder.send(&mut Err(DaemonError::RcsConnectionError)).unwrap();
            }
            DaemonRequest::Quit { responder } => {
                responder.send(true).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove() {
        let server =
            setup_fake_daemon_server_remove(|id| assert_eq!(id, "127.0.0.1:333399".to_owned()));
        remove_target(&server, &mut "127.0.0.1:333399".to_owned(), &mut 3).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove_retries() {
        let server = setup_fake_daemon_server_remove_with_err(|id| {
            assert_eq!(id, "127.0.0.1:333399".to_owned())
        });
        let mut retries = 3;
        match remove_target(&server, &mut "127.0.0.1:333399".to_owned(), &mut retries).await {
            Ok(()) => assert!(false),
            Err(_) => assert_eq!(retries, 0),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        let ssh_port = 12345;
        let server = setup_fake_daemon_server_add(move |addr| {
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
