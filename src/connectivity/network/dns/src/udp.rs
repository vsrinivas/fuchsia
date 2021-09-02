// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::FuchsiaTime,
    async_trait::async_trait,
    fuchsia_async::net::UdpSocket,
    futures::FutureExt as _,
    pin_utils::pin_mut,
    std::{
        io,
        net::SocketAddr,
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

    async fn bind(addr: SocketAddr) -> io::Result<Self> {
        UdpSocket::bind(&addr).map(Self)
    }

    fn poll_recv_from(
        &self,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        let fut = self.recv_from(buf);
        pin_mut!(fut);
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
        pin_mut!(fut);
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
