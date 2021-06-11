// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

use {
    crate::net::EventedFd,
    futures::{
        future::Future,
        ready,
        task::{Context, Poll},
    },
    std::{
        io,
        net::{self, SocketAddr},
        ops::Deref,
        pin::Pin,
    },
};

fn new_socket_address_conversion_error() -> std::io::Error {
    io::Error::new(io::ErrorKind::Other, "socket address is not IPv4 or IPv6")
}

/// An I/O object representing a UDP socket.
#[derive(Debug)]
pub struct UdpSocket(DatagramSocket);

impl Deref for UdpSocket {
    type Target = DatagramSocket;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl UdpSocket {
    /// Creates an async UDP socket from the given address.
    ///
    /// See [`std::net::UdpSocket::bind()`].
    pub fn bind(addr: &SocketAddr) -> io::Result<UdpSocket> {
        let socket = net::UdpSocket::bind(addr)?;
        UdpSocket::from_socket(socket)
    }

    /// Creates an async UDP socket from a [`std::net::UdpSocket`].
    pub fn from_socket(socket: net::UdpSocket) -> io::Result<UdpSocket> {
        let socket: socket2::Socket = socket.into();
        socket.set_nonblocking(true)?;
        let socket = socket.into();
        let evented_fd = unsafe { EventedFd::new(socket)? };
        Ok(UdpSocket(DatagramSocket(evented_fd)))
    }

    /// Returns the socket address that this socket was created from.
    pub fn local_addr(&self) -> io::Result<SocketAddr> {
        self.0
            .local_addr()
            .and_then(|sa| sa.as_socket().ok_or_else(new_socket_address_conversion_error))
    }

    /// Receive a UDP datagram from the socket.
    ///
    /// Asynchronous version of [`std::net::UdpSocket::recv_from()`].
    pub fn recv_from<'a>(&'a self, buf: &'a mut [u8]) -> UdpRecvFrom<'a> {
        UdpRecvFrom { socket: self, buf }
    }

    /// Send a UDP datagram via the socket.
    ///
    /// Asynchronous version of [`std::net::UdpSocket::send_to()`].
    pub fn send_to<'a>(&'a self, buf: &'a [u8], addr: SocketAddr) -> SendTo<'a> {
        SendTo { socket: self, buf, addr: addr.into() }
    }

    /// Asynchronously send a datagram (possibly split over multiple buffers) via the socket.
    pub fn send_to_vectored<'a>(
        &'a self,
        bufs: &'a [io::IoSlice<'a>],
        addr: SocketAddr,
    ) -> SendToVectored<'a> {
        SendToVectored { socket: self, bufs, addr: addr.into() }
    }
}

/// An I/O object representing a datagram socket.
#[derive(Debug)]
pub struct DatagramSocket(EventedFd<socket2::Socket>);

impl Deref for DatagramSocket {
    type Target = EventedFd<socket2::Socket>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DatagramSocket {
    /// Create a new async datagram socket.
    pub fn new(domain: socket2::Domain, protocol: Option<socket2::Protocol>) -> io::Result<Self> {
        let socket = socket2::Socket::new(domain, socket2::Type::DGRAM.nonblocking(), protocol)?;
        let evented_fd = unsafe { EventedFd::new(socket)? };
        Ok(Self(evented_fd))
    }

    /// Returns the socket address that this socket was created from.
    pub fn local_addr(&self) -> io::Result<socket2::SockAddr> {
        self.0.as_ref().local_addr()
    }

    /// Receive a datagram asynchronously from the socket.
    ///
    /// The returned future will resolve with the number of bytes read and the source address of
    /// the datagram on success.
    pub fn recv_from<'a>(&'a self, buf: &'a mut [u8]) -> RecvFrom<'a> {
        RecvFrom { socket: self, buf }
    }

    /// Attempt to receive a datagram from the socket without blocking.
    pub fn async_recv_from(
        &self,
        buf: &mut [u8],
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<(usize, socket2::SockAddr)>> {
        ready!(EventedFd::poll_readable(&self.0, cx))?;
        // SAFETY: socket2::Socket::recv_from takes a `&mut [MaybeUninit<u8>]`, so it's necessary to
        // type-pun `&mut [u8]`. This is safe because the bytes are known to be initialized, and
        // MaybeUninit's layout is guaranteed to be equivalent to its wrapped type.
        let buf = unsafe {
            std::slice::from_raw_parts_mut(
                buf.as_mut_ptr() as *mut core::mem::MaybeUninit<u8>,
                buf.len(),
            )
        };
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

    /// Send a datagram via the socket to the given address.
    ///
    /// The returned future will resolve with the number of bytes sent on success.
    pub fn send_to<'a>(&'a self, buf: &'a [u8], addr: socket2::SockAddr) -> SendTo<'a> {
        SendTo { socket: self, buf, addr }
    }

    /// Attempt to send a datagram via the socket without blocking.
    pub fn async_send_to(
        &self,
        buf: &[u8],
        addr: &socket2::SockAddr,
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

    /// Send a datagram (possibly split over multiple buffers) via the socket.
    pub fn send_to_vectored<'a>(
        &'a self,
        bufs: &'a [io::IoSlice<'a>],
        addr: socket2::SockAddr,
    ) -> SendToVectored<'a> {
        SendToVectored { socket: self, bufs, addr }
    }

    /// Attempt to send a datagram (possibly split over multiple buffers) via the socket without
    /// blocking.
    pub fn async_send_to_vectored<'a>(
        &self,
        bufs: &'a [io::IoSlice<'a>],
        addr: &socket2::SockAddr,
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<usize>> {
        ready!(EventedFd::poll_writable(&self.0, cx))?;
        match self.0.as_ref().send_to_vectored(bufs, addr) {
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

    /// Sets the value of the `SO_BROADCAST` option for this socket.
    ///
    /// When enabled, this socket is allowed to send packets to a broadcast address.
    pub fn set_broadcast(&self, broadcast: bool) -> io::Result<()> {
        self.0.as_ref().set_broadcast(broadcast)
    }

    /// Gets the value of the `SO_BROADCAST` option for this socket.
    pub fn broadcast(&self) -> io::Result<bool> {
        self.0.as_ref().broadcast()
    }
}

/// Future returned by [`UdpSocket::recv_from()`].
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct UdpRecvFrom<'a> {
    socket: &'a UdpSocket,
    buf: &'a mut [u8],
}

impl<'a> Future for UdpRecvFrom<'a> {
    type Output = io::Result<(usize, SocketAddr)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let (received, addr) = ready!(this.socket.0.async_recv_from(this.buf, cx))?;
        Poll::Ready(
            addr.as_socket()
                .ok_or_else(new_socket_address_conversion_error)
                .map(|addr| (received, addr)),
        )
    }
}

/// Future returned by [`DatagramSocket::recv_from()`].
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct RecvFrom<'a> {
    socket: &'a DatagramSocket,
    buf: &'a mut [u8],
}

impl<'a> Future for RecvFrom<'a> {
    type Output = io::Result<(usize, socket2::SockAddr)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let (received, addr) = ready!(this.socket.async_recv_from(this.buf, cx))?;
        Poll::Ready(Ok((received, addr)))
    }
}

/// Future returned by [`DatagramSocket::send_to()`].
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct SendTo<'a> {
    socket: &'a DatagramSocket,
    buf: &'a [u8],
    addr: socket2::SockAddr,
}

impl<'a> Future for SendTo<'a> {
    type Output = io::Result<usize>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.socket.async_send_to(self.buf, &self.addr, cx)
    }
}

/// Future returned by [`DatagramSocket::send_to_vectored()`].
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct SendToVectored<'a> {
    socket: &'a DatagramSocket,
    bufs: &'a [io::IoSlice<'a>],
    addr: socket2::SockAddr,
}

impl<'a> Future for SendToVectored<'a> {
    type Output = io::Result<usize>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.socket.async_send_to_vectored(self.bufs, &self.addr, cx)
    }
}
