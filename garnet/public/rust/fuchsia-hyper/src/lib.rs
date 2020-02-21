// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hyper_rustls;
use rustls;

use {
    fidl_fuchsia_net_stack::{StackMarker, StackSynchronousProxy},
    fuchsia_async::{
        net::{TcpConnector, TcpStream},
        EHandle,
    },
    fuchsia_component::client::connect_channel_to_service_at,
    fuchsia_zircon as zx,
    futures::{
        compat::Compat,
        future::{Future, FutureExt, TryFutureExt},
        io::{self, AsyncReadExt},
        ready,
        task::{Context, Poll, SpawnExt},
    },
    hyper::{
        client::{
            connect::{Connect, Connected, Destination},
            Client,
        },
        Body,
    },
    std::net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6, ToSocketAddrs},
    std::pin::Pin,
};

/// A Fuchsia-compatible hyper client configured for making HTTP requests.
pub type HttpClient = Client<HyperConnector, Body>;

/// A Fuchsia-compatible hyper client configured for making HTTP and HTTPS requests.
pub type HttpsClient = Client<hyper_rustls::HttpsConnector<HyperConnector>, Body>;

/// A future that yields a hyper-compatible TCP stream.
pub struct HyperTcpConnector(Result<TcpConnector, Option<io::Error>>);

impl Future for HyperTcpConnector {
    type Output = Result<(Compat<TcpStream>, Connected), io::Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let connector = self.0.as_mut().map_err(|x| x.take().unwrap())?;
        let stream = ready!(connector.poll_unpin(cx)?);
        Poll::Ready(Ok((stream.compat(), Connected::new())))
    }
}

/// A Fuchsia-compatible implementation of hyper's `Connect` trait which allows
/// creating a TcpStream to a particular destination.
pub struct HyperConnector;

impl Connect for HyperConnector {
    type Transport = Compat<TcpStream>;
    type Error = io::Error;
    type Future = Compat<HyperTcpConnector>;
    fn connect(&self, dst: Destination) -> Self::Future {
        let res = (|| {
            let host = dst.host();
            let port = match dst.port() {
                Some(port) => port,
                None => {
                    if dst.scheme() == "https" {
                        443
                    } else {
                        80
                    }
                }
            };

            let addr = if let Some(addr) = parse_ip_addr(host, port) {
                addr
            } else {
                // TODO(cramertj): smarter DNS-- nonblocking, don't just pick first addr
                (host, port).to_socket_addrs()?.next().ok_or_else(|| {
                    io::Error::new(io::ErrorKind::Other, "destination resolved to no address")
                })?
            };
            TcpStream::connect(addr)
        })();
        HyperTcpConnector(res.map_err(Some)).compat()
    }
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP requests.
pub fn new_client() -> HttpClient {
    Client::builder().executor(EHandle::local().compat()).build(HyperConnector)
}

pub fn new_https_client_dangerous(tls: rustls::ClientConfig) -> HttpsClient {
    let https = hyper_rustls::HttpsConnector::from((HyperConnector, tls));
    Client::builder().executor(EHandle::local().compat()).build(https)
}

/// Returns a new Fuchsia-compatible hyper client for making HTTP and HTTPS requests.
pub fn new_https_client() -> HttpsClient {
    let mut tls = rustls::ClientConfig::new();
    tls.root_store.add_server_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);
    new_https_client_dangerous(tls)
}

fn parse_ip_addr(host: &str, port: u16) -> Option<SocketAddr> {
    if let Ok(addr) = host.parse::<Ipv4Addr>() {
        return Some(SocketAddr::V4(SocketAddrV4::new(addr, port)));
    }

    // IpV6 literals are always in []
    if !host.starts_with("[") || !host.ends_with(']') {
        return None;
    }

    let host = &host[1..host.len() - 1];

    // IPv6 addresses with zones always contains "%25", which is "%" URL encoded.
    let mut host_parts = host.splitn(2, "%25");

    let addr = host_parts.next()?.parse::<Ipv6Addr>().ok()?;

    let scope_id = match host_parts.next() {
        Some(zone_id) => {
            // rfc6874 section 4 states:
            //
            //     The security considerations from the URI syntax specification
            //     [RFC3986] and the IPv6 Scoped Address Architecture specification
            //     [RFC4007] apply.  In particular, this URI format creates a specific
            //     pathway by which a deceitful zone index might be communicated, as
            //     mentioned in the final security consideration of the Scoped Address
            //     Architecture specification.  It is emphasised that the format is
            //     intended only for debugging purposes, but of course this intention
            //     does not prevent misuse.
            //
            //     To limit this risk, implementations MUST NOT allow use of this format
            //     except for well-defined usages, such as sending to link-local
            //     addresses under prefix fe80::/10.  At the time of writing, this is
            //     the only well-defined usage known.
            //
            // Since the only known use-case of IPv6 Zone Identifiers on Fuchsia is to communicate
            // with link-local devices, restrict addresse to link-local zone identifiers.
            //
            // TODO: use Ipv6Addr::is_unicast_link_local_strict when available in stable rust.
            if addr.segments()[..4] != [0xfe80, 0, 0, 0] {
                return None;
            }

            // TODO: validate that the value matches rfc6874 grammar `ZoneID = 1*( unreserved / pct-encoded )`.
            match zone_id.parse::<u32>() {
                Ok(zone_id_num) => zone_id_num,
                Err(_) => {
                    let (proxy, server) = zx::Channel::create().expect("failed to create channel");
                    connect_channel_to_service_at::<StackMarker>(server, "/svc")
                        .expect("failed to connect to netstack");
                    let mut netstack = StackSynchronousProxy::new(proxy);
                    let interface = netstack
                        .list_interfaces(zx::Time::INFINITE)
                        .expect("failed to list interfaces")
                        .into_iter()
                        .find(|interface| interface.properties.name == zone_id);
                    match interface {
                        Some(interface) => interface.id as u32,
                        None => return None,
                    }
                }
            }
        }
        None => 0,
    };

    Some(SocketAddr::V6(SocketAddrV6::new(addr, port, 0, scope_id)))
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async::Executor;

    #[test]
    fn can_create_client() {
        let _exec = Executor::new().unwrap();
        let _client = new_client();
    }

    #[test]
    fn can_create_https_client() {
        let _exec = Executor::new().unwrap();
        let _client = new_https_client();
    }

    #[test]
    fn test_parse_ipv4_addr() {
        let addr = parse_ip_addr("1.2.3.4", 8080);
        let expected = "1.2.3.4:8080".parse::<SocketAddr>().unwrap();
        assert_eq!(addr, Some(expected));
    }

    #[test]
    fn test_parse_invalid_addresses() {
        assert_eq!(parse_ip_addr("1.2.3", 8080), None);
        assert_eq!(parse_ip_addr("1.2.3.4.5", 8080), None);
        assert_eq!(parse_ip_addr("localhost", 8080), None);
        assert_eq!(parse_ip_addr("[fe80::1:2:3:4", 8080), None);
        assert_eq!(parse_ip_addr("[[fe80::1:2:3:4]", 8080), None);
    }

    #[test]
    fn test_parse_ipv6_addr() {
        let addr = parse_ip_addr("[fe80::1:2:3:4]", 8080);
        let expected = "[fe80::1:2:3:4]:8080".parse::<SocketAddr>().unwrap();
        assert_eq!(addr, Some(expected));
    }

    #[test]
    fn test_parse_ipv6_addr_with_zone() {
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        let addr = parse_ip_addr("[fe80::1:2:3:4%250]", 8080);
        assert_eq!(addr, Some(SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 0))));

        let addr = parse_ip_addr("[fe80::1:2:3:4%252]", 8080);
        assert_eq!(addr, Some(SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 2))));
    }

    #[test]
    fn test_parse_ipv6_addr_with_zone_supports_interface_names() {
        let expected = "fe80::1:2:3:4".parse::<Ipv6Addr>().unwrap();

        let addr = parse_ip_addr("[fe80::1:2:3:4%25lo]", 8080);
        assert_eq!(addr, Some(SocketAddr::V6(SocketAddrV6::new(expected, 8080, 0, 1))));

        assert_eq!(parse_ip_addr("[fe80::1:2:3:4%25unknownif]", 8080), None);
    }

    #[test]
    fn test_parse_ipv6_addr_with_zone_must_be_local() {
        assert_eq!(parse_ip_addr("[fe81::1:2:3:4%252]", 8080), None);
    }
}
