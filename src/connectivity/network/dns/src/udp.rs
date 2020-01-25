// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_async::net::UdpSocket,
    std::{io, net::SocketAddr},
    trust_dns_proto::udp,
};

/// A Fuchsia-compatible implementation of trust-dns's `UdpSocket` trait which allows
/// creating a UdpSocket to a particular destination.
pub struct DnsUdpSocket(UdpSocket);

#[async_trait]
impl udp::UdpSocket for DnsUdpSocket {
    async fn bind(addr: &SocketAddr) -> io::Result<Self> {
        UdpSocket::bind(addr).map(|u| Self(u))
    }

    async fn recv_from(&mut self, buf: &mut [u8]) -> io::Result<(usize, SocketAddr)> {
        self.0.recv_from(buf).await
    }

    async fn send_to(&mut self, buf: &[u8], target: &SocketAddr) -> io::Result<usize> {
        self.0.send_to(buf, *target).await
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{FuchsiaExec, FuchsiaTime};
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

    #[test]
    fn test_next_random_socket() {
        use trust_dns_proto::tests::next_random_socket_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        next_random_socket_test::<DnsUdpSocket, FuchsiaExec>(exec)
    }

    #[test]
    fn test_udp_stream_ipv4() {
        use trust_dns_proto::tests::udp_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        udp_stream_test::<DnsUdpSocket, FuchsiaExec>(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), exec)
    }

    #[test]
    fn test_udp_stream_ipv6() {
        use trust_dns_proto::tests::udp_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        udp_stream_test::<DnsUdpSocket, FuchsiaExec>(
            IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)),
            exec,
        )
    }

    #[test]
    fn test_udp_client_stream_ipv4() {
        use trust_dns_proto::tests::udp_client_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        udp_client_stream_test::<DnsUdpSocket, FuchsiaExec, FuchsiaTime>(
            IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)),
            exec,
        )
    }

    #[test]
    fn test_udp_client_stream_ipv6() {
        use trust_dns_proto::tests::udp_client_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        udp_client_stream_test::<DnsUdpSocket, FuchsiaExec, FuchsiaTime>(
            IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)),
            exec,
        )
    }
}
