// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta sockets.

use {HandleBase, Handle, HandleRef};
use {sys, Status, into_result};

use std::ptr;

/// An object representing a Magenta
/// [socket](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Message-Passing_Sockets-and-Channels).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Socket(Handle);

impl HandleBase for Socket {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Socket(handle)
    }
}

/// Options for creating a socket pair.
#[repr(u32)]
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
    /// [mx_socket_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/socket_create.md).
    pub fn create(opts: SocketOpts) -> Result<(Socket, Socket), Status> {
        unsafe {
            let mut out0 = 0;
            let mut out1 = 0;
            let status = sys::mx_socket_create(opts as u32, &mut out0, &mut out1);
            into_result(status, ||
                (Self::from_handle(Handle(out0)),
                    Self::from_handle(Handle(out1))))
        }
    }

    /// Write the given bytes into the socket.
    /// Return value (on success) is number of bytes actually written.
    ///
    /// Wraps
    /// [mx_socket_write](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/socket_write.md).
    pub fn write(&self, opts: SocketWriteOpts, bytes: &[u8]) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::mx_socket_write(self.raw_handle(), opts as u32, bytes.as_ptr(), bytes.len(),
                &mut actual)
        };
        into_result(status, || actual)
    }

    /// Read the given bytes from the socket.
    /// Return value (on success) is number of bytes actually read.
    ///
    /// Wraps
    /// [mx_socket_read](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/socket_read.md).
    pub fn read(&self, opts: SocketReadOpts, bytes: &mut [u8]) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::mx_socket_read(self.raw_handle(), opts as u32, bytes.as_mut_ptr(), bytes.len(),
                &mut actual)
        };
        into_result(status, || actual)
    }

    /// Close half of the socket, so attempts by the other side to write will fail.
    ///
    /// Implements the `MX_SOCKET_HALF_CLOSE` option of
    /// [mx_socket_write](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/socket_write.md).
    pub fn half_close(&self) -> Result<(), Status> {
        let status = unsafe { sys::mx_socket_write(self.raw_handle(), sys::MX_SOCKET_HALF_CLOSE,
            ptr::null(), 0, ptr::null_mut()) };
        into_result(status, || ())
    }
}
