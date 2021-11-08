// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

use crate::{IcmpSocket, Ip, Ipv4, Ipv6, TryFromSockAddr as _};
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

    /// Binds this to an interface so that packets can only flow in/out via the specified
    /// interface.
    ///
    /// Implemented by setting the `SO_BINDTODEVICE` socket option.
    ///
    /// See [`fuchsia_async::net::DatagramSocket::bind_device()`].
    fn bind_device(&self, interface: Option<&[u8]>) -> std::io::Result<()> {
        self.bind_device(interface)
    }
}

/// Create a new ICMP socket.
pub fn new_icmp_socket<I: Ip>() -> std::io::Result<fasync::net::DatagramSocket> {
    fasync::net::DatagramSocket::new(I::DOMAIN, Some(I::PROTOCOL))
}

/// Extension trait on [`Ip`] for Fuchsia-specific functionality.
pub trait IpExt: Ip {
    /// Socket domain.
    const DOMAIN_FIDL: fidl_fuchsia_posix_socket::Domain;
}

impl IpExt for Ipv4 {
    const DOMAIN_FIDL: fidl_fuchsia_posix_socket::Domain = fidl_fuchsia_posix_socket::Domain::Ipv4;
}

impl IpExt for Ipv6 {
    const DOMAIN_FIDL: fidl_fuchsia_posix_socket::Domain = fidl_fuchsia_posix_socket::Domain::Ipv6;
}
