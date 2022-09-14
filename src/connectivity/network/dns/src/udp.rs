// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::FuchsiaTime,
    async_trait::async_trait,
    fuchsia_async::net::UdpSocket,
    futures::FutureExt as _,
    std::{
        io,
        net::{Ipv4Addr, Ipv6Addr, SocketAddr},
        task::{Context, Poll},
    },
    trust_dns_proto::udp,
};

/// A Fuchsia-compatible implementation of trust-dns's `UdpSocket` trait which allows
/// creating a UdpSocket to a particular destination.
pub struct DnsUdpSocket(UdpSocket);

#[async_trait]
impl udp::UdpSocket for DnsUdpSocket {
    type Time = FuchsiaTime;

    async fn connect_with_bind(_addr: SocketAddr, bind_addr: SocketAddr) -> io::Result<Self> {
        let socket = Self::bind(bind_addr).await?;

        // TODO(https://fxbug.dev/108817): Consider calling connect on the
        // socket. Doing so isn't strictly necessary and is disabled within the
        // provided Trust-DNS implementations. As a result, the same behavior is
        // currently implemented here. See
        // https://github.com/bluejekyll/trust-dns/commit/e712a2c031572a128d720d6c763a83fe57399d7f

        Ok(socket)
    }

    async fn connect(addr: SocketAddr) -> io::Result<Self> {
        let bind_addr = match addr {
            SocketAddr::V4(_addr) => (Ipv4Addr::UNSPECIFIED, 0).into(),
            SocketAddr::V6(_addr) => (Ipv6Addr::UNSPECIFIED, 0).into(),
        };

        Self::connect_with_bind(addr, bind_addr).await
    }

    async fn bind(addr: SocketAddr) -> io::Result<Self> {
        UdpSocket::bind(&addr).map(Self)
    }

    fn poll_recv_from(
        &self,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        let fut = self.recv_from(buf);
        futures::pin_mut!(fut);
        fut.poll_unpin(cx)
    }

    async fn recv_from(&self, buf: &mut [u8]) -> io::Result<(usize, SocketAddr)> {
        let Self(socket) = self;
        socket.recv_from(buf).await
    }

    fn poll_send_to(
        &self,
        cx: &mut Context<'_>,
        buf: &[u8],
        target: SocketAddr,
    ) -> Poll<io::Result<usize>> {
        let fut = self.send_to(buf, target);
        futures::pin_mut!(fut);
        fut.poll_unpin(cx)
    }

    async fn send_to(&self, buf: &[u8], target: SocketAddr) -> io::Result<usize> {
        let Self(socket) = self;
        socket.send_to(buf, target).await
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{FuchsiaExec, FuchsiaTime};

    use net_declare::std::ip;

    #[test]
    fn test_next_random_socket() {
        use trust_dns_proto::tests::next_random_socket_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        next_random_socket_test::<DnsUdpSocket, FuchsiaExec>(exec)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_udp_stream_ipv4() {
        use trust_dns_proto::tests::udp_stream_test;
        udp_stream_test::<DnsUdpSocket>(ip!("127.0.0.1")).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_udp_stream_ipv6() {
        use trust_dns_proto::tests::udp_stream_test;
        udp_stream_test::<DnsUdpSocket>(ip!("::1")).await
    }

    #[test]
    fn test_udp_client_stream_ipv4() {
        use trust_dns_proto::tests::udp_client_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        udp_client_stream_test::<DnsUdpSocket, FuchsiaExec, FuchsiaTime>(ip!("127.0.0.1"), exec)
    }

    #[test]
    fn test_udp_client_stream_ipv6() {
        use trust_dns_proto::tests::udp_client_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        udp_client_stream_test::<DnsUdpSocket, FuchsiaExec, FuchsiaTime>(ip!("::1"), exec)
    }
}
