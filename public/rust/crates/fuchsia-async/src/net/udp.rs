// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::task;
use futures::{Poll, Future};
use std::io;
use std::marker::Unpin;
use std::mem::PinMut;
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
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        try_ready!(EventedFd::poll_readable(&self.0, cx));
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

    pub fn send_to<B: AsRef<[u8]>>(self, buf: B, addr: SocketAddr) -> SendTo<B> {
        SendTo(Some((buf, addr, self)))
    }

    pub fn async_send_to(
        &self, buf: &[u8], addr: SocketAddr, cx: &mut task::Context,
    ) -> Poll<io::Result<()>> {
        try_ready!(EventedFd::poll_writable(&self.0, cx));
        match self.0.as_ref().send_to(buf, addr) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_write(cx);
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok(_) => Poll::Ready(Ok(())),
        }
    }
}

pub struct RecvFrom<B>(Option<B>, Option<UdpSocket>);

impl<B> Unpin for RecvFrom<B> {}

impl<B> Future for RecvFrom<B>
where
    B: AsMut<[u8]>,
{
    type Output = io::Result<(UdpSocket, B, usize, SocketAddr)>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context)
        -> Poll<Self::Output>
    {
        let this = &mut *self;
        let addr;
        let received;
        {
            let socket = this.1.as_mut().expect("polled a RecvFrom after completion");
            let buf = this.0.as_mut().expect("polled a RecvFrom after completion");
            let (r, a) = try_ready!(socket.async_recv_from(buf.as_mut(), cx));
            addr = a;
            received = r;
        }
        let socket = this.1.take().unwrap();
        let buffer = this.0.take().unwrap();
        Poll::Ready(Ok((socket, buffer, received, addr)))
    }
}

pub struct SendTo<B>(Option<(B, SocketAddr, UdpSocket)>);

impl<B> Unpin for SendTo<B> {}

impl<B> Future for SendTo<B>
where
    B: AsRef<[u8]>,
{
    type Output = io::Result<UdpSocket>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context)
        -> Poll<Self::Output>
    {
        {
            let (buf, addr, socket) = self.0.as_mut().expect("polled a SendTo after completion");
            try_ready!(socket.async_send_to(buf.as_ref(), *addr, cx));
        }
        let (_, _, socket) = self.0.take().unwrap();
        Poll::Ready(Ok(socket))
    }
}

#[cfg(test)]
mod tests {
    use Executor;
    use futures::TryFutureExt;
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
            .map_ok(|(_sock, recvbuf, received, sender)| {
                assert_eq!(addr, sender);
                assert_eq!(received, buf.len());
                assert_eq!(&buf, &recvbuf.as_slice());
            });

        exec.run_singlethreaded(fut).expect("failed to run udp socket test");
    }
}
