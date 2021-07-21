// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_core::ffx_plugin,
    ffx_target_add_args::AddCommand,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_net as net,
    regex::Regex,
    std::net::IpAddr,
};

#[cfg(not(test))]
use std::ffi::CString;

#[ffx_plugin()]
pub async fn add(daemon_proxy: bridge::DaemonProxy, cmd: AddCommand) -> Result<()> {
    let v6bracket = Regex::new(r"^\[([^\]]+)\](:\d+)?$")?;
    let v4port = Regex::new(r"^(\d+\.\d+\.\d+\.\d+)(:\d+)?$")?;
    let with_scope = Regex::new(r"^(.*)%(.*)$")?;

    let (addr, port) = if let Some(caps) =
        v6bracket.captures(cmd.addr.as_str()).or_else(|| v4port.captures(cmd.addr.as_str()))
    {
        (caps.get(1).map(|x| x.as_str()).unwrap(), caps.get(2).map(|x| x.as_str()))
    } else {
        (cmd.addr.as_str(), None)
    };

    let port = if let Some(port) = port { Some(port[1..].parse::<u16>()?) } else { None };

    let (addr, scope) = if let Some(caps) = with_scope.captures(addr) {
        (caps.get(1).map(|x| x.as_str()).unwrap(), Some(caps.get(2).map(|x| x.as_str()).unwrap()))
    } else {
        (addr, None)
    };

    let addr = addr.parse::<IpAddr>()?;

    #[cfg(not(test))]
    let scope_id = if let Some(scope) = scope {
        unsafe {
            let scope = CString::new(scope).unwrap();
            libc::if_nametoindex(scope.as_ptr())
        }
    } else {
        0
    };

    #[cfg(test)]
    let scope_id = if let Some(scope) = scope { scope.parse()? } else { 0 };

    let ip = match addr {
        IpAddr::V6(i) => net::IpAddress::Ipv6(net::Ipv6Address { addr: i.octets().into() }),
        IpAddr::V4(i) => net::IpAddress::Ipv4(net::Ipv4Address { addr: i.octets().into() }),
    };
    let mut addr = if let Some(port) = port {
        bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort { ip, port, scope_id })
    } else {
        bridge::TargetAddrInfo::Ip(bridge::TargetIp { ip, scope_id })
    };

    if let Err(e) = daemon_proxy.add_target(&mut addr).await? {
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
        add(server, AddCommand { addr: "123.210.123.210".to_owned() }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_port() {
        let server = setup_fake_daemon_server(|addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
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
        add(server, AddCommand { addr: "123.210.123.210:2310".to_owned() }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6() {
        let server = setup_fake_daemon_server(|addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 0,
                })
            )
        });
        add(server, AddCommand { addr: "f000::1".to_owned() }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6_port() {
        let server = setup_fake_daemon_server(|addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 0,
                    port: 65,
                })
            )
        });
        add(server, AddCommand { addr: "[f000::1]:65".to_owned() }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6_scope_id() {
        let server = setup_fake_daemon_server(|addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 1,
                })
            )
        });
        add(server, AddCommand { addr: "f000::1%1".to_owned() }).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_v6_scope_id_port() {
        let server = setup_fake_daemon_server(|addr| {
            assert_eq!(
                addr,
                bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
                    ip: net::IpAddress::Ipv6(net::Ipv6Address {
                        addr: "f000::1".parse::<std::net::Ipv6Addr>().unwrap().octets().into()
                    }),
                    scope_id: 1,
                    port: 640,
                })
            )
        });
        add(server, AddCommand { addr: "[f000::1%1]:640".to_owned() }).await.unwrap();
    }
}
