// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    errors::ffx_bail,
    fidl::endpoints::ProtocolMarker,
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
    let (tc_proxy, tc_server) = fidl::endpoints::create_proxy::<bridge::TargetCollectionMarker>()?;
    if let Err(e) = daemon_proxy
        .connect_to_service(bridge::TargetCollectionMarker::NAME, tc_server.into_channel())
        .await?
    {
        ffx_bail!("Error opening target collection service: {:?}", e)
    }
    if let Err(e) = tc_proxy.add_target(&mut addr).await {
        ffx_bail!("Error adding target: {:?}", e)
    }
    Ok(())
}

pub async fn remove_target(
    daemon_proxy: &bridge::DaemonProxy,
    target_id: &mut String,
    retries: &mut i32,
) -> Result<()> {
    let (tc_proxy, tc_server) = fidl::endpoints::create_proxy::<bridge::TargetCollectionMarker>()?;
    if let Err(e) = daemon_proxy
        .connect_to_service(bridge::TargetCollectionMarker::NAME, tc_server.into_channel())
        .await?
    {
        ffx_bail!("Error opening target collection service: {:?}", e)
    }
    while *retries > 0 {
        match tc_proxy.remove_target(target_id).await {
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

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::{endpoints::RequestStream, AsyncChannel},
        fidl_fuchsia_developer_bridge::{
            DaemonProxy, DaemonRequest, TargetCollectionRequest, TargetCollectionRequestStream,
        },
        fuchsia_async::futures::TryStreamExt,
    };

    fn setup_fake_daemon_proxy<R: 'static>(mut handle_request: R) -> DaemonProxy
    where
        R: FnMut(fidl::endpoints::Request<<DaemonProxy as fidl::endpoints::Proxy>::Protocol>),
    {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<
            <DaemonProxy as fidl::endpoints::Proxy>::Protocol,
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

    fn setup_fake_daemon_server_add<T: 'static + Fn(bridge::TargetAddrInfo) + Send + Copy>(
        test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::ConnectToService { name: _, server_channel, responder } => {
                fuchsia_async::Task::spawn(async move {
                    let channel = AsyncChannel::from_channel(server_channel).unwrap();
                    let mut stream = TargetCollectionRequestStream::from_channel(channel);
                    while let Ok(Some(req)) = stream.try_next().await {
                        match req {
                            TargetCollectionRequest::AddTarget { ip, responder } => {
                                (test)(ip);
                                responder.send().unwrap();
                            }
                            r => panic!("unexpected request: {:?}", r),
                        }
                    }
                })
                .detach();
                responder.send(&mut Ok(())).unwrap();
            }
            r => panic!("unexpected request: {:?}", r),
        })
    }

    fn setup_fake_daemon_server_remove<T: 'static + Fn(String) + Send + Copy>(
        test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::ConnectToService { name: _, server_channel, responder } => {
                fuchsia_async::Task::spawn(async move {
                    let channel = AsyncChannel::from_channel(server_channel).unwrap();
                    let mut stream = TargetCollectionRequestStream::from_channel(channel);
                    while let Ok(Some(req)) = stream.try_next().await {
                        match req {
                            TargetCollectionRequest::RemoveTarget { target_id, responder } => {
                                (test)(target_id);
                                responder.send(true).unwrap();
                            }
                            r => panic!("unexpected request: {:?}", r),
                        }
                    }
                })
                .detach();
                responder.send(&mut Ok(())).unwrap();
            }
            r => panic!("unexpected request: {:?}", r),
        })
    }

    fn setup_fake_daemon_server_remove_with_err<T: 'static + Fn(String) + Send>(
        _test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::ConnectToService { name: _, server_channel, responder } => {
                fuchsia_async::Task::spawn(async move {
                    let channel = AsyncChannel::from_channel(server_channel).unwrap();
                    let mut stream = TargetCollectionRequestStream::from_channel(channel);
                    while let Ok(Some(req)) = stream.try_next().await {
                        match req {
                            TargetCollectionRequest::RemoveTarget {
                                target_id: _,
                                responder: _,
                            } => {}
                            r => panic!("unexpected request: {:?}", r),
                        }
                    }
                })
                .detach();
                responder.send(&mut Ok(())).unwrap();
            }
            r => panic!("unexpected request: {:?}", r),
        })
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
}
