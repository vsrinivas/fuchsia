// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{io, fmt};

use futures::{Async, Future, Poll};
use mio::fuchsia::{EventedHandle, FuchsiaReady};
use zircon::{self, AsHandleRef};

use tokio_core::reactor::{Handle, PollEvented};

use super::would_block;

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

    /// Reads a message on the socket and registers this `Socket` as
    /// needing a read on receiving a `zircon::Status::ErrShouldWait`.
    pub fn read_from(&self, opts: zircon::SocketReadOpts, buf: &mut [u8]) -> io::Result<usize> {
        let signals = self.evented.poll_ready(FuchsiaReady::from(
                          zircon::ZX_SOCKET_READABLE |
                          zircon::ZX_SOCKET_PEER_CLOSED).into());

        match signals {
            Async::NotReady => Err(would_block()),
            Async::Ready(ready) => {
                let signals = FuchsiaReady::from(ready).into_zx_signals();
                if zircon::ZX_SOCKET_PEER_CLOSED.intersects(signals) {
                    Err(io::ErrorKind::ConnectionAborted.into())
                } else {
                    let res = self.socket.read(opts, buf);
                    if res == Err(zircon::Status::ErrShouldWait) {
                        self.evented.need_read();
                    }
                    res.map_err(io::Error::from)
                }
            }
        }
    }

    /// Creates a future which, when polled, will attempt to perform a read
    /// on the socket.
    ///
    /// The returned future will complete after a _single_ call to read has
    /// succeeded.  The future will resolve to the socket, buffer, and the
    /// number of bytes read.
    ///
    /// An error during reading will cause the socket and buffer to get
    /// destroyed and the status will be returned.
    ///
    /// The AsMut<[u8]> means you can pass an `&mut [u8]`, `Vec<u8>`, or other
    /// array-like collections of `u8` elements.
    pub fn read<T>(self, opts: zircon::SocketReadOpts, buf: T) -> ReadFuture<T>
        where T: AsMut<[u8]>
    {
        ReadFuture(Some((self, buf, opts)))
    }

    /// Writes a message on the socket and registers this `Socket` as
    /// needing a write on receiving a `zircon::Status::ErrShouldWait`.
    pub fn write_into(&self, opts: zircon::SocketWriteOpts, buf: &[u8]) -> io::Result<usize> {
        let signals = self.evented.poll_ready(FuchsiaReady::from(
                          zircon::ZX_SOCKET_WRITABLE |
                          zircon::ZX_SOCKET_PEER_CLOSED).into());

        match signals {
            Async::NotReady => Err(would_block()),
            Async::Ready(ready) => {
                let signals = FuchsiaReady::from(ready).into_zx_signals();
                if zircon::ZX_SOCKET_PEER_CLOSED.intersects(signals) {
                    Err(io::ErrorKind::ConnectionAborted.into())
                } else {
                    let res = self.socket.write(opts, buf);
                    if res == Err(zircon::Status::ErrShouldWait) {
                        self.evented.need_write();
                    }
                    res.map_err(io::Error::from)
                }
            }
        }
    }

    /// Creates a future which, when polled, will attempt to perform a write
    /// on the socket.
    ///
    /// The returned future will complete after a _single_ call to `write` has
    /// succeeded.  The future will resolve to the socket, buffer, and the
    /// number of bytes written.
    ///
    /// An error during reading will cause the socket and buffer to get
    /// destroyed and the status will be returned.
    ///
    /// The AsRef<[u8]> means you can pass an `&mut [u8]`, `Vec<u8>`, or other
    /// array-like collections of `u8` elements.
    pub fn write<T>(self, opts: zircon::SocketWriteOpts, buf: T) -> WriteFuture<T>
        where T: AsRef<[u8]>
    {
        WriteFuture(Some((self, buf, opts)))
    }
}

impl fmt::Debug for Socket {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.socket.fmt(f)
    }
}

/// A future used to read bytes from a socket.
///
/// This is created by the `Socket::read` method.
#[must_use = "futures do nothing unless polled"]
pub struct ReadFuture<T>(Option<(Socket, T, zircon::SocketReadOpts)>);

impl<T> Future for ReadFuture<T>
    where T: AsMut<[u8]>,
{
    type Item = (Socket, T, usize);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, io::Error> {
        let num_read;
        {
            let (ref socket, ref mut buf, opts) =
                *self.0.as_mut().expect("polled a ReadFuture after completion");
            num_read = try_nb!(socket.read_from(opts, buf.as_mut()));
        }
        let (socket, buf, _opts) = self.0.take().unwrap();
        Ok(Async::Ready((socket, buf, num_read)))
    }
}

/// A future used to write bytes into a socket.
///
/// This is created by the `Socket::write` method.
#[must_use = "futures do nothing unless polled"]
pub struct WriteFuture<T>(Option<(Socket, T, zircon::SocketWriteOpts)>);

impl<T> Future for WriteFuture<T>
    where T: AsRef<[u8]>,
{
    type Item = (Socket, T, usize);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, io::Error> {
        let num_written;
        {
            let (ref socket, ref buf, opts) =
                *self.0.as_ref().expect("polled a WriteFuture after completion");
            num_written = try_nb!(socket.write_into(opts, buf.as_ref()));
        }
        let (socket, buf, _opts) = self.0.take().unwrap();
        Ok(Async::Ready((socket, buf, num_written)))
    }
}

#[cfg(test)]
mod tests {
    use tokio_core::reactor::{Core, Timeout};
    use std::time::Duration;
    use zircon::{self, MessageBuf, SocketOpts, SocketReadOpts, SocketWriteOpts};
    use super::*;

    #[test]
    fn can_read_write() {
        let mut core = Core::new().unwrap();
        let handle = &core.handle();
        let bytes: &'static [u8] = &[0,1,2,3];

        let (tx, rx) = zircon::Socket::create(SocketOpts::Default).unwrap();
        let (tx, rx) = (
            Socket::from_socket(tx, handle).unwrap(),
            Socket::from_socket(rx, handle).unwrap(),
        );

        let receiver = rx.read(SocketReadOpts::Default, [0; 4]).map(|(_socket, buf, num_read)| {
            assert_eq!(num_read, buf.len());
            assert_eq!(buf, bytes);
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map(|()| {
            panic!("did not receive message in time!");
        });
        let receiver = receiver.select(rcv_timeout).map(|_| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().and_then(|()|{
            tx.write(SocketWriteOpts::Default, bytes)
        }).map(|(_socket, _buf, num_written)| {
            assert_eq!(num_written, bytes.len());
        });

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }
}
