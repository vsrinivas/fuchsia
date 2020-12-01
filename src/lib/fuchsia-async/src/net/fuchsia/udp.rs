// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::net::EventedFd,
    futures::{
        future::Future,
        ready,
        task::{Context, Poll},
    },
    std::{
        io,
        marker::Unpin,
        net::{self, SocketAddr},
        ops::Deref,
        pin::Pin,
    },
};

/// An I/O object representing a UDP socket.
pub struct UdpSocket(EventedFd<net::UdpSocket>);

impl Deref for UdpSocket {
    type Target = EventedFd<net::UdpSocket>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl UdpSocket {
    pub fn bind(addr: &SocketAddr) -> io::Result<UdpSocket> {
        let socket = net::UdpSocket::bind(addr)?;
        UdpSocket::from_socket(socket)
    }

    pub fn from_socket(socket: net::UdpSocket) -> io::Result<UdpSocket> {
        let socket: socket2::Socket = socket.into();
        let () = socket.set_nonblocking(true)?;
        let socket = socket.into_udp_socket();
        let socket = unsafe { EventedFd::new(socket)? };
        Ok(UdpSocket(socket))
    }

    pub fn local_addr(&self) -> io::Result<SocketAddr> {
        self.0.as_ref().local_addr()
    }

    pub fn recv_from<'a>(&'a self, buf: &'a mut [u8]) -> RecvFrom<'a> {
        RecvFrom { socket: self, buf }
    }

    pub fn async_recv_from(
        &self,
        buf: &mut [u8],
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        ready!(EventedFd::poll_readable(&self.0, cx))?;
        match self.0.as_ref().recv_from(buf) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_read(cx);
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok((size, addr)) => Poll::Ready(Ok((size, addr))),
        }
    }

    pub fn send_to<'a>(&'a self, buf: &'a [u8], addr: SocketAddr) -> SendTo<'a> {
        SendTo { socket: self, buf, addr }
    }

    pub fn async_send_to(
        &self,
        buf: &[u8],
        addr: SocketAddr,
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<usize>> {
        ready!(EventedFd::poll_writable(&self.0, cx))?;
        match self.0.as_ref().send_to(buf, addr) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_write(cx);
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok(size) => Poll::Ready(Ok(size)),
        }
    }

    pub fn set_broadcast(&self, broadcast: bool) -> io::Result<()> {
        self.0.as_ref().set_broadcast(broadcast)
    }

    pub fn broadcast(&self) -> io::Result<bool> {
        self.0.as_ref().broadcast()
    }
}

pub struct RecvFrom<'a> {
    socket: &'a UdpSocket,
    buf: &'a mut [u8],
}

impl<'a> Unpin for RecvFrom<'a> {}

impl<'a> Future for RecvFrom<'a> {
    type Output = io::Result<(usize, SocketAddr)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let (received, addr) = ready!(this.socket.async_recv_from(this.buf, cx))?;
        Poll::Ready(Ok((received, addr)))
    }
}

pub struct SendTo<'a> {
    socket: &'a UdpSocket,
    buf: &'a [u8],
    addr: SocketAddr,
}

impl<'a> Unpin for SendTo<'a> {}

impl<'a> Future for SendTo<'a> {
    type Output = io::Result<usize>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.socket.async_send_to(self.buf, self.addr, cx)
    }
}
