// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::BufMut;
use futures::task;
use futures::{Async, Future, Poll};
use std::io;
use std::net::{self, SocketAddr};
use std::ops::Deref;

use net::{set_nonblock, EventedFd};
use std::os::unix::io::AsRawFd;

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
        set_nonblock(socket.as_raw_fd())?;

        unsafe { Ok(UdpSocket(EventedFd::new(socket)?)) }
    }

    pub fn recv_from<B: BufMut>(self, buf: B) -> RecvFrom<B> {
        RecvFrom(Some(buf), Some(self))
    }

    pub fn async_recv_from(
        &self, buf: &mut [u8], cx: &mut task::Context,
    ) -> Poll<(usize, SocketAddr), io::Error> {
        try_ready!(EventedFd::poll_readable(&self.0, cx));
        match self.0.as_ref().recv_from(buf) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_read(cx);
                    return Ok(Async::Pending);
                }
                return Err(e);
            }
            Ok((size, addr)) => Ok(Async::Ready((size, addr))),
        }
    }
}

pub struct RecvFrom<B: BufMut>(Option<B>, Option<UdpSocket>);

impl<B> Future for RecvFrom<B>
where
    B: BufMut,
{
    type Item = (UdpSocket, B, SocketAddr);
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let addr;
        {
            let socket = self.1.as_mut().expect("polled a RecvFrom after completion");
            let buf = self.0.as_mut().expect("polled a RecvFrom after completion");
            let (s, a) = try_ready!(socket.async_recv_from(unsafe { buf.bytes_mut() }, cx));
            println!("RecvFrom: advancing {} bytes", s);
            unsafe {
                buf.advance_mut(s);
            }
            addr = a;
        }
        let socket = self.1.take().unwrap();
        let buffer = self.0.take().unwrap();
        Ok(Async::Ready((socket, buffer, addr)))
    }
}
