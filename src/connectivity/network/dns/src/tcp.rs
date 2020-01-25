// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_async::net::TcpStream,
    futures::io::{AsyncRead, AsyncWrite},
    futures::task::{Context, Poll},
    std::net::SocketAddr,
    std::{io, pin::Pin},
    trust_dns_proto::tcp::Connect,
};

/// A Fuchsia-compatible implementation of trust-dns's `Connect` trait which allows
/// creating a `TcpStream` to a particular destination.
pub struct DnsTcpStream(TcpStream);

#[async_trait]
impl Connect for DnsTcpStream {
    type Transport = DnsTcpStream;

    async fn connect(addr: SocketAddr) -> io::Result<Self::Transport> {
        let connector = TcpStream::connect(addr)?;
        connector.await.map(|tcp| DnsTcpStream(tcp))
    }
}

impl AsyncRead for DnsTcpStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.0).poll_read(cx, buf)
    }
}

impl AsyncWrite for DnsTcpStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<Result<usize, io::Error>> {
        Pin::new(&mut self.0).poll_write(cx, buf)
    }
    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Pin::new(&mut self.0).poll_flush(cx)
    }

    fn poll_close(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.0).poll_close(cx)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use crate::{FuchsiaExec, FuchsiaTime};
    use std::net::Ipv6Addr;
    use std::net::{IpAddr, Ipv4Addr};

    #[test]
    fn test_tcp_stream_ipv4() {
        use trust_dns_proto::tests::tcp_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        tcp_stream_test::<DnsTcpStream, FuchsiaExec, FuchsiaTime>(
            IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)),
            exec,
        )
    }

    #[test]
    fn test_tcp_stream_ipv6() {
        use trust_dns_proto::tests::tcp_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        tcp_stream_test::<DnsTcpStream, FuchsiaExec, FuchsiaTime>(
            IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)),
            exec,
        )
    }

    #[test]
    fn test_tcp_client_stream_ipv4() {
        use trust_dns_proto::tests::tcp_client_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        tcp_client_stream_test::<DnsTcpStream, FuchsiaExec, FuchsiaTime>(
            IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)),
            exec,
        )
    }

    #[test]
    fn test_tcp_client_stream_ipv6() {
        use trust_dns_proto::tests::tcp_client_stream_test;
        let exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        tcp_client_stream_test::<DnsTcpStream, FuchsiaExec, FuchsiaTime>(
            IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)),
            exec,
        )
    }
}
