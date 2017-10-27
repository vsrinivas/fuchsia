// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::io::{self, Read, Write};

use futures::{Async, Poll};
use mio::fuchsia::EventedHandle;
use zircon::{self, AsHandleRef};

use tokio_core::reactor::{Handle, PollEvented};
use tokio_io::{AsyncRead, AsyncWrite};

/// An I/O object representing a `Socket`.
pub struct Socket {
    socket: zircon::Socket,
    evented: PollEvented<EventedHandle>,
}

impl AsRef<zircon::Socket> for Socket {
    fn as_ref(&self) -> &zircon::Socket {
        &self.socket
    }
}

impl AsHandleRef for Socket {
    fn as_handle_ref(&self) -> zircon::HandleRef {
        self.socket.as_handle_ref()
    }
}

impl From<Socket> for zircon::Socket {
    fn from(socket: Socket) -> zircon::Socket {
        socket.socket
    }
}

impl Socket {
    /// Creates a new `Socket` from a previously-created `zircon::Socket`.
    pub fn from_socket(socket: zircon::Socket, handle: &Handle) -> io::Result<Socket> {
        // This is safe because the `EventedHandle` will only live as long as the
        // underlying `zircon::Socket`.
        let ev_handle = unsafe { EventedHandle::new(socket.raw_handle()) };
        let evented = PollEvented::new(ev_handle, handle)?;

        Ok(Socket { evented, socket })
    }

    /// Test whether this socket is ready to be read or not.
    ///
    /// If the socket is *not* readable then the current task is scheduled to
    /// get a notification when the socket does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is readable again.
    pub fn poll_read(&self) -> Async<()> {
        self.evented.poll_read()
    }

    /// Test whether this socket is ready to be written to or not.
    ///
    /// If the socket is *not* writable then the current task is scheduled to
    /// get a notification when the socket does become writable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is writable again.
    pub fn poll_write(&self) -> Async<()> {
        self.evented.poll_write()
    }

    // Private helper for reading without `&mut` self.
    // This is used in the impls of `Read` for `Socket` and `&Socket`.
    fn read_nomut(&self, buf: &mut [u8]) -> io::Result<usize> {
        if let Async::NotReady = self.poll_read() {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        let res = self.socket.read(buf);
        if res == Err(zircon::Status::SHOULD_WAIT) {
            self.evented.need_read();
        }
        res.map_err(io::Error::from)
    }

    // Private helper for writing without `&mut` self.
    // This is used in the impls of `Write` for `Socket` and `&Socket`.
    fn write_nomut(&self, buf: &[u8]) -> io::Result<usize> {
        if let Async::NotReady = self.poll_write() {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        let res = self.socket.write(buf);
        if res == Err(zircon::Status::SHOULD_WAIT) {
            self.evented.need_write();
        }
        res.map_err(io::Error::from)
    }
}

impl fmt::Debug for Socket {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.socket.fmt(f)
    }
}

impl Read for Socket {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.read_nomut(buf)
    }
}

impl Write for Socket {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.write_nomut(buf)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl AsyncRead for Socket {
    // Asserts that `Socket::read` doesn't examine the buffer passed into it.
    unsafe fn prepare_uninitialized_buffer(&self, _: &mut [u8]) -> bool {
        false
    }
}

impl AsyncWrite for Socket {
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        Ok(().into())
    }
}

impl<'a> Read for &'a Socket {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.read_nomut(buf)
    }
}

impl<'a> Write for &'a Socket {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.write_nomut(buf)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl<'a> AsyncRead for &'a Socket {
    // Asserts that `Socket::read` doesn't examine the buffer passed into it.
    unsafe fn prepare_uninitialized_buffer(&self, _: &mut [u8]) -> bool {
        false
    }
}

impl<'a> AsyncWrite for &'a Socket {
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        Ok(().into())
    }
}

#[cfg(test)]
mod tests {
    use Bytes;
    use futures::{Future, Stream};
    use tokio_core::reactor::{Core, Timeout};
    use tokio_io::io;
    use std::time::Duration;
    use super::*;

    #[test]
    fn can_read_write() {
        let mut core = Core::new().unwrap();
        let handle = &core.handle();
        let bytes = &[0,1,2,3];

        let (tx, rx) = zircon::Socket::create().unwrap();
        let (tx, rx) = (
            Socket::from_socket(tx, handle).unwrap(),
            Socket::from_socket(rx, handle).unwrap(),
        );

        let receive_future = rx.framed(Bytes).into_future().map(|(bytes_mut_opt, _rx)| {
            let buf = bytes_mut_opt.unwrap();
            assert_eq!(buf.as_ref(), bytes);
        }).map_err(|(err, _rx)| err);

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map(|()| {
            panic!("did not receive message in time!");
        });

        let receiver = receive_future
                            .select(rcv_timeout)
                            .map(|_| ())
                            .map_err(|(err, _)| err);

        // Sends a message after the timeout has passed
        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap()
                        .and_then(|()| io::write_all(tx, bytes))
                        .map(|_tx| ());

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }
}
