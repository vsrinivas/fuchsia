// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon sockets.

use {AsHandleRef, HandleBased, Handle, HandleRef, Peered};
use {sys, Status, ok};

use std::ptr;

/// An object representing a Zircon
/// [socket](https://fuchsia.googlesource.com/zircon/+/master/docs/concepts.md#Message-Passing_Sockets-and-Channels).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
pub struct Socket(Handle);
impl_handle_based!(Socket);
impl Peered for Socket {}

/// Options for creating a socket pair.
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum SocketOpts {
    /// Default options.
    Default = 0,
}

impl Default for SocketOpts {
    fn default() -> Self {
        SocketOpts::Default
    }
}

/// Options for writing into a socket.
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum SocketWriteOpts {
    /// Default options.
    Default = 0,
}

impl Default for SocketWriteOpts {
    fn default() -> Self {
        SocketWriteOpts::Default
    }
}

/// Options for reading from a socket.
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum SocketReadOpts {
    /// Default options.
    Default = 0,
}

impl Default for SocketReadOpts {
    fn default() -> Self {
        SocketReadOpts::Default
    }
}


impl Socket {
    /// Create a socket, accessed through a pair of endpoints. Data written
    /// into one may be read from the other.
    ///
    /// Wraps
    /// [zx_socket_create](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/socket_create.md).
    pub fn create(opts: SocketOpts) -> Result<(Socket, Socket), Status> {
        unsafe {
            let mut out0 = 0;
            let mut out1 = 0;
            let status = sys::zx_socket_create(opts as u32, &mut out0, &mut out1);
            ok(status)?;
            Ok((
                Self::from(Handle::from_raw(out0)),
                Self::from(Handle::from_raw(out1))
            ))
        }
    }

    /// Write the given bytes into the socket.
    /// Return value (on success) is number of bytes actually written.
    ///
    /// Wraps
    /// [zx_socket_write](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/socket_write.md).
    pub fn write(&self, opts: SocketWriteOpts, bytes: &[u8]) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::zx_socket_write(self.raw_handle(), opts as u32, bytes.as_ptr(), bytes.len(),
                &mut actual)
        };
        ok(status).map(|()| actual)
    }

    /// Read the given bytes from the socket.
    /// Return value (on success) is number of bytes actually read.
    ///
    /// Wraps
    /// [zx_socket_read](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/socket_read.md).
    pub fn read(&self, opts: SocketReadOpts, bytes: &mut [u8]) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::zx_socket_read(self.raw_handle(), opts as u32, bytes.as_mut_ptr(),
                bytes.len(), &mut actual)
        };
        ok(status)
            .map(|()| actual)
            .map_err(|status| {
                // If an error is returned then actual is undefined, so to be safe
                // we set it to 0 and ignore any data that is set in bytes.
                actual = 0;
                status
            })
    }

    /// Close half of the socket, so attempts by the other side to write will fail.
    ///
    /// Implements the `ZX_SOCKET_HALF_CLOSE` option of
    /// [zx_socket_write](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/socket_write.md).
    pub fn half_close(&self) -> Result<(), Status> {
        let status = unsafe { sys::zx_socket_write(self.raw_handle(), sys::ZX_SOCKET_HALF_CLOSE,
            ptr::null(), 0, ptr::null_mut()) };
        ok(status)
    }

    pub fn outstanding_read_bytes(&self) -> Result<usize, Status> {
        let mut outstanding = 0;
        let status = unsafe {
            sys::zx_socket_read(self.raw_handle(), 0, ptr::null_mut(), 0, &mut outstanding)
        };
        ok(status).map(|()| outstanding)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn socket_basic() {
        let (s1, s2) = Socket::create(SocketOpts::Default).unwrap();

        // Write in one end and read it back out the other.
        assert_eq!(s1.write(SocketWriteOpts::Default, b"hello").unwrap(), 5);

        let mut read_vec = vec![0; 8];
        assert_eq!(s2.read(SocketReadOpts::Default, &mut read_vec).unwrap(), 5);
        assert_eq!(&read_vec[0..5], b"hello");

        // Try reading when there is nothing to read.
        assert_eq!(s2.read(SocketReadOpts::Default, &mut read_vec), Err(Status::SHOULD_WAIT));

        // Close the socket from one end.
        assert!(s1.half_close().is_ok());
        assert_eq!(s2.read(SocketReadOpts::Default, &mut read_vec), Err(Status::BAD_STATE));
        assert_eq!(s1.write(SocketWriteOpts::Default, b"fail"), Err(Status::BAD_STATE));

        // Writing in the other direction should still work.
        assert_eq!(s1.read(SocketReadOpts::Default, &mut read_vec), Err(Status::SHOULD_WAIT));
        assert_eq!(s2.write(SocketWriteOpts::Default, b"back").unwrap(), 4);
        assert_eq!(s1.read(SocketReadOpts::Default, &mut read_vec).unwrap(), 4);
        assert_eq!(&read_vec[0..4], b"back");
    }
}
