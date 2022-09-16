// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error, FfxError},
    ffx_core::ffx_plugin,
    ffx_target_add_args::AddCommand,
    fidl_fuchsia_developer_ffx::{self as ffx, TargetCollectionProxy},
    fidl_fuchsia_net as net,
    futures::TryStreamExt,
    netext::parse_address_parts,
    std::net::IpAddr,
};

#[ffx_plugin(TargetCollectionProxy = "daemon::protocol")]
pub async fn add(target_collection_proxy: TargetCollectionProxy, cmd: AddCommand) -> Result<()> {
    let (addr, scope, port) =
        parse_address_parts(cmd.addr.as_str()).map_err(|e| ffx_error!("{}", e))?;
    let scope_id = if let Some(scope) = scope {
        match netext::get_verified_scope_id(scope) {
            Ok(res) => res,
            Err(_e) => {
                return Err(ffx_error!(
                    "Cannot add target, as scope ID '{scope}' is not a valid interface name or index"
                )
                .into());
            }
        }
    } else {
        0
    };
    let ip = match addr {
        IpAddr::V6(i) => net::IpAddress::Ipv6(net::Ipv6Address { addr: i.octets().into() }),
        IpAddr::V4(i) => net::IpAddress::Ipv4(net::Ipv4Address { addr: i.octets().into() }),
    };
    let mut addr = if let Some(port) = port {
        ffx::TargetAddrInfo::IpPort(ffx::TargetIpPort { ip, port, scope_id })
    } else {
        ffx::TargetAddrInfo::Ip(ffx::TargetIp { ip, scope_id })
    };

    let (client, server) = fidl::endpoints::create_endpoints::<ffx::AddTargetResponder_Marker>()?;
    target_collection_proxy.add_target(
        &mut addr,
        ffx::AddTargetConfig {
            verify_connection: Some(!cmd.nowait),
            ..ffx::AddTargetConfig::EMPTY
        },
        client,
    )?;
    let mut stream = server.into_stream()?;
    let res = if let Ok(Some(req)) = stream.try_next().await {
        match req {
            ffx::AddTargetResponder_Request::Success { .. } => Ok(()),
            ffx::AddTargetResponder_Request::Error { err, .. } => Err(err),
        }
    } else {
        ffx_bail!("ffx lost connection to the daemon before receiving a response.");
    };
    res.map_err(|e| {
        let err = e.connection_error.unwrap();
        let logs = e.connection_error_logs.map(|v| v.join("\n"));
        let is_default_target = false;
        let target = Some(format!("{}", cmd.addr));
        FfxError::TargetConnectionError { err, target, is_default_target, logs }.into()
    })
}

#[cfg(test)]
mod test {
    use super::*;

    fn setup_fake_target_collection<T: 'static + Fn(ffx::TargetAddrInfo) + Send>(
        test: T,
    ) -> TargetCollectionProxy {
        setup_fake_target_collection_proxy(move |req| match req {
            ffx::TargetCollectionRequest::AddTarget {
                ip, config: _, add_target_responder, ..
            } => {
                let add_target_responder = add_target_responder.into_proxy().unwrap();
                test(ip);
                add_target_responder.success().unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        let server = setup_fake_target_collection(|addr| {
            assert_eq!(
                addr,
                ffx::TargetAddrInfo::Ip(ffx::TargetIp {
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
        add(server, AddCommand { addr: "123.210.123.210".to_owned(), nowait: true }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_port() {
        let server = setup_fake_target_collection(|addr| {
            assert_eq!(
                addr,
                ffx::TargetAddrInfo::IpPort(ffx::TargetIpPort {
                    ip: net::IpAddress::Ipv4(net::Ipv4Address {
                        addr: "123.210.123.210"
                            .parse::<std::net::Ipv4Addr>()
                            .unwrap()
                            .octets()
                            .into()
                    }),
                    scope_id: 0,
                    port: 2310,
                })
            )
        });
        add(server, AddCommand { addr: "123.210.123.210:2310".to_owned(), nowait: true })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6() {
        let server = setup_fake_target_collection(|addr| {
            assert_eq!(
                addr,
                ffx::TargetAddrInfo::Ip(ffx::TargetIp {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 0,
                })
            )
        });
        add(server, AddCommand { addr: "f000::1".to_owned(), nowait: true }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6_port() {
        let server = setup_fake_target_collection(|addr| {
            assert_eq!(
                addr,
                ffx::TargetAddrInfo::IpPort(ffx::TargetIpPort {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 0,
                    port: 65,
                })
            )
        });
        add(server, AddCommand { addr: "[f000::1]:65".to_owned(), nowait: true }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6_scope_id() {
        let server = setup_fake_target_collection(|addr| {
            assert_eq!(
                addr,
                ffx::TargetAddrInfo::Ip(ffx::TargetIp {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 1,
                })
            )
        });
        add(server, AddCommand { addr: "f000::1%1".to_owned(), nowait: true }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6_scope_id_port() {
        let server = setup_fake_target_collection(|addr| {
            assert_eq!(
                addr,
                ffx::TargetAddrInfo::IpPort(ffx::TargetIpPort {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 1,
                    port: 640,
                })
            )
        });
        add(server, AddCommand { addr: "[f000::1%1]:640".to_owned(), nowait: true }).await.unwrap();
    }
}
