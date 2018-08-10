// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use futures::{Poll, task};
use futures::io::{self, AsyncRead, AsyncWrite, Initializer};
use zx::{self, AsHandleRef};

use RWHandle;

/// An I/O object representing a `Socket`.
pub struct Socket(RWHandle<zx::Socket>);

impl AsRef<zx::Socket> for Socket {
    fn as_ref(&self) -> &zx::Socket {
        self.0.get_ref()
    }
}

impl AsHandleRef for Socket {
    fn as_handle_ref(&self) -> zx::HandleRef {
        self.0.get_ref().as_handle_ref()
    }
}

impl From<Socket> for zx::Socket {
    fn from(socket: Socket) -> zx::Socket {
        socket.0.into_inner()
    }
}

impl Socket {
    /// Creates a new `Socket` from a previously-created `zx::Socket`.
    pub fn from_socket(socket: zx::Socket) -> Result<Socket, zx::Status> {
        Ok(Socket(RWHandle::new(socket)?))
    }

    /// Test whether this socket is ready to be read or not.
    ///
    /// If the socket is *not* readable then the current task is scheduled to
    /// get a notification when the socket does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is readable again.
    fn poll_read(&self, cx: &mut task::Context) -> Poll<Result<(), zx::Status>> {
        self.0.poll_read(cx)
    }

    /// Test whether this socket is ready to be written to or not.
    ///
    /// If the socket is *not* writable then the current task is scheduled to
    /// get a notification when the socket does become writable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is writable again.
    fn poll_write(&self, cx: &mut task::Context) -> Poll<Result<(), zx::Status>> {
        self.0.poll_write(cx)
    }

    // Private helper for reading without `&mut` self.
    // This is used in the impls of `Read` for `Socket` and `&Socket`.
    fn read_nomut(&self, buf: &mut [u8], cx: &mut task::Context) -> Poll<Result<usize, zx::Status>> {
        try_ready!(self.poll_read(cx));
        let res = self.0.get_ref().read(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_read(cx)?;
            return Poll::Pending;
        }
        if res == Err(zx::Status::PEER_CLOSED) {
            return Poll::Ready(Ok(0));
        }
        Poll::Ready(res)
    }

    // Private helper for writing without `&mut` self.
    // This is used in the impls of `Write` for `Socket` and `&Socket`.
    fn write_nomut(&self, buf: &[u8], cx: &mut task::Context) -> Poll<Result<usize, zx::Status>> {
        try_ready!(self.poll_write(cx));
        let res = self.0.get_ref().write(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_write(cx)?;
            Poll::Pending
        } else {
            Poll::Ready(res)
        }
    }
}

impl fmt::Debug for Socket {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.0.get_ref().fmt(f)
    }
}

impl AsyncRead for Socket {
    unsafe fn initializer(&self) -> Initializer {
        // This is safe because `zx::Socket::read` does not examine
        // the buffer before reading into it.
        Initializer::nop()
    }

    fn poll_read(&mut self, cx: &mut task::Context, buf: &mut [u8])
        -> Poll<io::Result<usize>>
    {
        self.read_nomut(buf, cx).map_err(Into::into)
    }
}

impl AsyncWrite for Socket {
    fn poll_write(&mut self, cx: &mut task::Context, buf: &[u8])
        -> Poll<io::Result<usize>>
    {
        self.write_nomut(buf, cx).map_err(Into::into)
    }

    fn poll_flush(&mut self, _: &mut task::Context)
        -> Poll<io::Result<()>>
    {
        Poll::Ready(Ok(()))
    }

    fn poll_close(&mut self, _: &mut task::Context)
        -> Poll<io::Result<()>>
    {
        Poll::Ready(Ok(()))
    }
}

impl<'a> AsyncRead for &'a Socket {
    unsafe fn initializer(&self) -> Initializer {
        // This is safe because `zx::Socket::read` does not examine
        // the buffer before reading into it.
        Initializer::nop()
    }

    fn poll_read(&mut self, cx: &mut task::Context, buf: &mut [u8])
        -> Poll<io::Result<usize>>
    {
        self.read_nomut(buf, cx).map_err(Into::into)
    }
}

impl<'a> AsyncWrite for &'a Socket {
    fn poll_write(&mut self, cx: &mut task::Context, buf: &[u8])
        -> Poll<io::Result<usize>>
    {
        self.write_nomut(buf, cx).map_err(Into::into)
    }

    fn poll_flush(&mut self, _: &mut task::Context) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(&mut self, _: &mut task::Context) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {Executor, Timer, TimeoutExt, temp::{TempAsyncWriteExt, TempAsyncReadExt}};
    use futures::future::{FutureExt, TryFutureExt};
    use zx::prelude::*;

    #[test]
    fn can_read_write() {
        let mut exec = Executor::new().unwrap();
        let bytes = &[0,1,2,3];

        let (tx, rx) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let (tx, rx) = (
            Socket::from_socket(tx).unwrap(),
            Socket::from_socket(rx).unwrap(),
        );

        let receive_future = rx.read_to_end(vec![]).map_ok(|(_socket, buf)| {
            assert_eq!(&*buf, bytes);
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future.on_timeout(
                            300.millis().after_now(),
                            || panic!("timeout"));

        // Sends a message after the timeout has passed
        let sender = Timer::new(100.millis().after_now())
                        .then(|()| tx.write_all(bytes))
                        .map_ok(|_tx| ());

        let done = receiver.try_join(sender);
        exec.run_singlethreaded(done).unwrap();
    }
}
