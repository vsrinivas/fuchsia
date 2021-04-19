// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides async Socket type wrapped around an emulated zircon socket.

// TODO(ctiller): merge this implementation with the implementation in zircon_handle?

use super::{Handle, HandleBased};
use fuchsia_zircon_status as zx_status;
use futures::future::poll_fn;
use futures::prelude::*;
use futures::ready;
use std::borrow::BorrowMut;
use std::pin::Pin;
use std::task::{Context, Poll};

/// An I/O object representing a `Socket`.
pub struct Socket {
    socket: super::Socket,
}

impl AsRef<super::Socket> for Socket {
    fn as_ref(&self) -> &super::Socket {
        &self.socket
    }
}

impl super::AsHandleRef for Socket {
    fn as_handle_ref(&self) -> super::HandleRef<'_> {
        self.socket.as_handle_ref()
    }
}

impl std::fmt::Debug for Socket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.socket.fmt(f)
    }
}

impl Socket {
    /// Construct an `Socket` from an existing `emulated_handle::Socket`
    pub fn from_socket(socket: super::Socket) -> std::io::Result<Socket> {
        Ok(Socket { socket })
    }

    /// Convert AsyncSocket back into a regular socket
    pub fn into_zx_socket(self) -> super::Socket {
        self.socket
    }

    /// Polls for the next data on the socket, appending it to the end of |out| if it has arrived.
    /// Not very useful for a non-datagram socket as it will return all available data
    /// on the socket.
    pub fn poll_datagram(
        &self,
        cx: &mut Context<'_>,
        out: &mut Vec<u8>,
    ) -> Poll<Result<usize, zx_status::Status>> {
        let avail = self.socket.outstanding_read_bytes()?;
        let len = out.len();
        out.resize(len + avail, 0);
        let (_, mut tail) = out.split_at_mut(len);
        match ready!(self.socket.poll_read(&mut tail, cx)) {
            Err(zx_status::Status::PEER_CLOSED) => Poll::Ready(Ok(0)),
            Err(e) => Poll::Ready(Err(e)),
            Ok(bytes) => {
                if bytes == avail {
                    Poll::Ready(Ok(bytes))
                } else {
                    Poll::Ready(Err(zx_status::Status::BAD_STATE))
                }
            }
        }
    }

    /// Reads the next datagram that becomes available onto the end of |out|.  Note: Using this
    /// multiple times concurrently is an error and the first one will never complete.
    pub async fn read_datagram<'a>(
        &'a self,
        out: &'a mut Vec<u8>,
    ) -> Result<usize, zx_status::Status> {
        poll_fn(move |cx| self.poll_datagram(cx, out)).await
    }
}

impl AsyncWrite for Socket {
    fn poll_write(
        self: Pin<&mut Self>,
        _cx: &mut std::task::Context<'_>,
        bytes: &[u8],
    ) -> Poll<Result<usize, std::io::Error>> {
        Poll::Ready(self.socket.write(bytes).map_err(|e| e.into()))
    }

    fn poll_flush(
        self: Pin<&mut Self>,
        _cx: &mut std::task::Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(
        mut self: Pin<&mut Self>,
        _cx: &mut std::task::Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        self.borrow_mut().socket = super::Socket::from_handle(Handle::invalid());
        Poll::Ready(Ok(()))
    }
}

impl AsyncRead for Socket {
    fn poll_read(
        self: Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
        bytes: &mut [u8],
    ) -> Poll<Result<usize, std::io::Error>> {
        match ready!(self.socket.poll_read(bytes, cx)) {
            Err(zx_status::Status::PEER_CLOSED) => Poll::Ready(Ok(0)),
            Ok(x) => {
                assert_ne!(x, 0);
                Poll::Ready(Ok(x))
            }
            Err(x) => Poll::Ready(Err(x.into())),
        }
    }
}

#[cfg(test)]
mod test {
    use super::super::{Socket, SocketOpts};
    use super::Socket as AsyncSocket;
    use futures::executor::block_on;
    use futures::prelude::*;
    use futures::task::noop_waker_ref;
    use std::pin::Pin;
    use std::task::Context;

    #[test]
    fn async_socket_write_read() {
        block_on(async move {
            let (a, b) = Socket::create(SocketOpts::STREAM).unwrap();
            let (mut a, mut b) =
                (AsyncSocket::from_socket(a).unwrap(), AsyncSocket::from_socket(b).unwrap());
            let mut buf = [0u8; 128];

            let mut cx = Context::from_waker(noop_waker_ref());

            let mut rx = b.read(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            assert!(Pin::new(&mut a.write(&[1, 2, 3])).poll(&mut cx).is_ready());
            rx.await.unwrap();
            assert_eq!(&buf[0..3], &[1, 2, 3]);

            let mut rx = a.read(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            assert!(Pin::new(&mut b.write(&[1, 2, 3])).poll(&mut cx).is_ready());
            rx.await.unwrap();
            assert_eq!(&buf[0..3], &[1, 2, 3]);
        })
    }
}
