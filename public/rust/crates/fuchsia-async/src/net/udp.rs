// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    pub fn recv_from<B: AsMut<[u8]>>(self, buf: B) -> RecvFrom<B> {
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
                    Ok(Async::Pending)
                } else {
                    Err(e)
                }
            }
            Ok((size, addr)) => Ok(Async::Ready((size, addr))),
        }
    }

    pub fn send_to<B: AsRef<[u8]>>(self, buf: B, addr: SocketAddr) -> SendTo<B> {
        SendTo(Some((buf, addr, self)))
    }

    pub fn async_send_to(
        &self, buf: &[u8], addr: SocketAddr, cx: &mut task::Context,
    ) -> Poll<(), io::Error> {
        try_ready!(EventedFd::poll_writable(&self.0, cx));
        match self.0.as_ref().send_to(buf, addr) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_write(cx);
                    Ok(Async::Pending)
                } else {
                    Err(e)
                }
            }
            Ok(_) => Ok(Async::Ready(())),
        }
    }
}

pub struct RecvFrom<B: AsMut<[u8]>>(Option<B>, Option<UdpSocket>);

impl<B> Future for RecvFrom<B>
where
    B: AsMut<[u8]>,
{
    type Item = (UdpSocket, B, usize, SocketAddr);
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let addr;
        let received;
        {
            let socket = self.1.as_mut().expect("polled a RecvFrom after completion");
            let buf = self.0.as_mut().expect("polled a RecvFrom after completion");
            let (r, a) = try_ready!(socket.async_recv_from(buf.as_mut(), cx));
            addr = a;
            received = r;
        }
        let socket = self.1.take().unwrap();
        let buffer = self.0.take().unwrap();
        Ok(Async::Ready((socket, buffer, received, addr)))
    }
}

pub struct SendTo<B: AsRef<[u8]>>(Option<(B, SocketAddr, UdpSocket)>);

impl<B> Future for SendTo<B>
where
    B: AsRef<[u8]>,
{
    type Item = UdpSocket;
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        {
            let (buf, addr, socket) = self.0.as_mut().expect("polled a SendTo after completion");
            try_ready!(socket.async_send_to(buf.as_ref(), *addr, cx));
        }
        let (_, _, socket) = self.0.take().unwrap();
        Ok(Async::Ready(socket))
    }
}

#[cfg(test)]
mod tests {
    use Executor;
    use futures::{FutureExt, future};
    use super::UdpSocket;

    #[test]
    fn send_recv() {
        let mut exec = Executor::new().expect("could not create executor");

        let addr = "127.0.0.1:29995".parse().unwrap();
        let buf = b"hello world";
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        let fut = socket.send_to(&buf, addr)
            .and_then(|socket| {
                let recvbuf = vec![0; 11];
                socket.recv_from(recvbuf)
            })
            .and_then(|(_sock, recvbuf, received, sender)| {
                assert_eq!(addr, sender);
                assert_eq!(received, buf.len());
                assert_eq!(&buf, &recvbuf.as_slice());
                future::ok(())
            });

        exec.run_singlethreaded(fut).expect("failed to run udp socket test");
    }
}
