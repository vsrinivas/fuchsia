// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::io::{self, Read, Write};

use futures::{Async, Poll};
use zx::{self, AsHandleRef};

use executor::EHandle;
use tokio_io::{AsyncRead, AsyncWrite};

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
    pub fn from_socket(socket: zx::Socket, ehandle: &EHandle) -> Result<Socket, zx::Status> {
        Ok(Socket(RWHandle::new(socket, ehandle)?))
    }

    /// Test whether this socket is ready to be read or not.
    ///
    /// If the socket is *not* readable then the current task is scheduled to
    /// get a notification when the socket does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is readable again.
    pub fn poll_read(&self) -> Poll<(), zx::Status> {
        self.0.poll_read()
    }

    /// Test whether this socket is ready to be written to or not.
    ///
    /// If the socket is *not* writable then the current task is scheduled to
    /// get a notification when the socket does become writable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is writable again.
    pub fn poll_write(&self) -> Poll<(), zx::Status> {
        self.0.poll_write()
    }

    // Private helper for reading without `&mut` self.
    // This is used in the impls of `Read` for `Socket` and `&Socket`.
    fn read_nomut(&self, buf: &mut [u8]) -> Result<usize, zx::Status> {
        if let Async::NotReady = self.poll_read()? {
            return Err(zx::Status::SHOULD_WAIT);
        }
        let res = self.0.get_ref().read(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_read()?;
        }
        res
    }

    // Private helper for writing without `&mut` self.
    // This is used in the impls of `Write` for `Socket` and `&Socket`.
    fn write_nomut(&self, buf: &[u8]) -> Result<usize, zx::Status> {
        if let Async::NotReady = self.poll_write()? {
            return Err(zx::Status::SHOULD_WAIT);
        }
        let res = self.0.get_ref().write(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_write()?;
        }
        res
    }
}

impl fmt::Debug for Socket {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.0.get_ref().fmt(f)
    }
}

impl Read for Socket {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.read_nomut(buf).map_err(Into::into)
    }
}

impl Write for Socket {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.write_nomut(buf).map_err(Into::into)
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
        self.read_nomut(buf).map_err(Into::into)
    }
}

impl<'a> Write for &'a Socket {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.write_nomut(buf).map_err(Into::into)
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
    use futures::{Future, Stream};
    use {Executor, Timeout};
    use tokio_io::io as asyncio;
    use tokio_io::codec::BytesCodec;
    use super::*;
    use zx::prelude::*;

    #[test]
    fn can_read_write() {
        let mut exec = Executor::new().unwrap();
        let ehandle = &exec.ehandle();
        let bytes = &[0,1,2,3];

        let (tx, rx) = zx::Socket::create().unwrap();
        let (tx, rx) = (
            Socket::from_socket(tx, ehandle).unwrap(),
            Socket::from_socket(rx, ehandle).unwrap(),
        );

        let receive_future = rx.framed(BytesCodec::new()).into_future().map(|(bytes_mut_opt, _rx)| {
            let buf = bytes_mut_opt.unwrap();
            assert_eq!(buf.as_ref(), bytes);
        }).map_err(|(err, _rx)| err);

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(300.millis().after_now(), &ehandle).unwrap().map(|()| {
            panic!("did not receive message in time!");
        });

        let receiver = receive_future.select(rcv_timeout).map(|_| ()).map_err(|(e, _)| e.into());

        // Sends a message after the timeout has passed
        let sender = Timeout::new(100.millis().after_now(), &ehandle).unwrap()
                        .and_then(|()| asyncio::write_all(tx, bytes))
                        .map(|_tx| ());

        let done = receiver.join(sender);
        exec.run_singlethreaded(done).unwrap();
    }
}
