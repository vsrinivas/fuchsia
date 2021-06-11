// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

use crate::{IcmpSocket, Ip, TryFromSockAddr as _};
use core::task::{Context, Poll};
use fuchsia_async as fasync;
use futures::ready;

impl<I> IcmpSocket<I> for fasync::net::DatagramSocket
where
    I: Ip,
{
    /// Async method for receiving an ICMP packet.
    ///
    /// See [`fuchsia_async::net::DatagramSocket::recv_from()`].
    fn async_recv_from(
        &self,
        buf: &mut [u8],
        cx: &mut Context<'_>,
    ) -> Poll<std::io::Result<(usize, I::Addr)>> {
        Poll::Ready(
            ready!(self.async_recv_from(buf, cx))
                .and_then(|(len, addr)| I::Addr::try_from(addr).map(|addr| (len, addr))),
        )
    }

    /// Async method for sending an ICMP packet.
    ///
    /// See [`fuchsia_async::net::DatagramSocket::send_to_vectored()`].
    fn async_send_to_vectored(
        &self,
        bufs: &[std::io::IoSlice<'_>],
        addr: &I::Addr,
        cx: &mut Context<'_>,
    ) -> Poll<std::io::Result<usize>> {
        self.async_send_to_vectored(bufs, &(*addr).clone().into(), cx)
    }
}

/// Create a new ICMP socket.
pub fn new_icmp_socket<I: Ip>() -> std::io::Result<fasync::net::DatagramSocket> {
    fasync::net::DatagramSocket::new(I::DOMAIN, Some(I::PROTOCOL))
}
