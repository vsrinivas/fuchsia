// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon sockets.

use crate::{object_get_info, object_get_property, object_set_property};
use crate::{ok, Status};
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Peered};
use crate::{ObjectQuery, Topic};
use crate::{Property, PropertyQuery, PropertyQueryGet, PropertyQuerySet};
use bitflags::bitflags;
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon
/// [socket](https://fuchsia.dev/fuchsia-src/concepts/kernel/concepts#message_passing_sockets_and_channels)
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Socket(Handle);
impl_handle_based!(Socket);
impl Peered for Socket {}

bitflags! {
    #[repr(transparent)]
    pub struct SocketOpts: u32 {
        const STREAM = 0 << 0;
        const DATAGRAM = 1 << 0;
    }
}

bitflags! {
    #[repr(transparent)]
    #[derive(Default)]
    pub struct SocketReadOpts: u32 {
        const PEEK = 1 << 3;
    }
}

bitflags! {
    #[repr(transparent)]
    #[derive(Default)]
    pub struct SocketWriteOpts: u32 {
    }
}

/// Write disposition to set on a zircon socket with
/// [zx_socket_set_disposition](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_set_disposition.md).
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum SocketWriteDisposition {
    /// Corresponds to `ZX_SOCKET_DISPOSITION_WRITE_ENABLED`.
    Enabled,
    /// Corresponds to `ZX_SOCKET_DISPOSITION_WRITE_DISABLED`.
    Disabled,
}

impl From<SocketWriteDisposition> for u32 {
    fn from(disposition: SocketWriteDisposition) -> Self {
        match disposition {
            SocketWriteDisposition::Enabled => sys::ZX_SOCKET_DISPOSITION_WRITE_ENABLED,
            SocketWriteDisposition::Disabled => sys::ZX_SOCKET_DISPOSITION_WRITE_DISABLED,
        }
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct SocketInfo {
    pub options: SocketOpts,
    pub rx_buf_max: usize,
    pub rx_buf_size: usize,
    pub rx_buf_available: usize,
    pub tx_buf_max: usize,
    pub tx_buf_size: usize,
}

impl Default for SocketInfo {
    fn default() -> SocketInfo {
        SocketInfo {
            options: SocketOpts::STREAM,
            rx_buf_max: 0,
            rx_buf_size: 0,
            rx_buf_available: 0,
            tx_buf_max: 0,
            tx_buf_size: 0,
        }
    }
}

impl From<sys::zx_info_socket_t> for SocketInfo {
    fn from(socket: sys::zx_info_socket_t) -> SocketInfo {
        SocketInfo {
            options: SocketOpts::from_bits_truncate(socket.options),
            rx_buf_max: socket.rx_buf_max,
            rx_buf_size: socket.rx_buf_size,
            rx_buf_available: socket.rx_buf_available,
            tx_buf_max: socket.tx_buf_max,
            tx_buf_size: socket.tx_buf_size,
        }
    }
}

// zx_info_socket_t is able to be safely replaced with a byte representation and is a PoD type.
struct SocketInfoQuery;
unsafe impl ObjectQuery for SocketInfoQuery {
    const TOPIC: Topic = Topic::SOCKET;
    type InfoTy = sys::zx_info_socket_t;
}

impl Socket {
    /// Create a socket, accessed through a pair of endpoints. Data written
    /// into one may be read from the other.
    ///
    /// Wraps
    /// [zx_socket_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_create.md).
    pub fn create(sock_opts: SocketOpts) -> Result<(Socket, Socket), Status> {
        unsafe {
            let mut out0 = 0;
            let mut out1 = 0;
            let status = sys::zx_socket_create(sock_opts.bits(), &mut out0, &mut out1);
            ok(status)?;
            Ok((Self::from(Handle::from_raw(out0)), Self::from(Handle::from_raw(out1))))
        }
    }

    /// Write the given bytes into the socket.
    /// Return value (on success) is number of bytes actually written.
    ///
    /// Wraps
    /// [zx_socket_write](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_write.md).
    pub fn write(&self, bytes: &[u8]) -> Result<usize, Status> {
        self.write_opts(bytes, SocketWriteOpts::default())
    }

    /// Write the given bytes into the socket, with options.
    /// Return value (on success) is number of bytes actually written.
    ///
    /// Wraps
    /// [zx_socket_write](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_write.md).
    pub fn write_opts(&self, bytes: &[u8], opts: SocketWriteOpts) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::zx_socket_write(
                self.raw_handle(),
                opts.bits(),
                bytes.as_ptr(),
                bytes.len(),
                &mut actual,
            )
        };
        ok(status).map(|()| actual)
    }

    /// Read the given bytes from the socket.
    /// Return value (on success) is number of bytes actually read.
    ///
    /// Wraps
    /// [zx_socket_read](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_read.md).
    pub fn read(&self, bytes: &mut [u8]) -> Result<usize, Status> {
        self.read_opts(bytes, SocketReadOpts::default())
    }

    /// Read the given bytes from the socket, with options.
    /// Return value (on success) is number of bytes actually read.
    ///
    /// Wraps
    /// [zx_socket_read](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_read.md).
    pub fn read_opts(&self, bytes: &mut [u8], opts: SocketReadOpts) -> Result<usize, Status> {
        let mut actual = 0;
        let status = unsafe {
            sys::zx_socket_read(
                self.raw_handle(),
                opts.bits(),
                bytes.as_mut_ptr(),
                bytes.len(),
                &mut actual,
            )
        };
        ok(status).map(|()| actual)
    }

    /// Close half of the socket, so attempts by the other side to write will fail.
    ///
    /// Implements the `ZX_SOCKET_DISPOSITION_WRITE_DISABLED` option of
    /// [zx_socket_set_disposition](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_set_disposition.md).
    pub fn half_close(&self) -> Result<(), Status> {
        self.set_disposition(None, Some(SocketWriteDisposition::Disabled))
    }

    /// Sets the disposition of write calls for a socket handle and its peer.
    ///
    /// Wraps
    /// [zx_socket_set_disposition](https://fuchsia.dev/fuchsia-src/reference/syscalls/socket_set_disposition.md).
    pub fn set_disposition(
        &self,
        disposition: Option<SocketWriteDisposition>,
        disposition_peer: Option<SocketWriteDisposition>,
    ) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_socket_set_disposition(
                self.raw_handle(),
                disposition.map(u32::from).unwrap_or(0),
                disposition_peer.map(u32::from).unwrap_or(0),
            )
        };
        ok(status)
    }

    /// Returns the number of bytes available on the socket.
    pub fn outstanding_read_bytes(&self) -> Result<usize, Status> {
        Ok(self.info()?.rx_buf_available)
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_SOCKET topic.
    pub fn info(&self) -> Result<SocketInfo, Status> {
        let mut info = sys::zx_info_socket_t::default();
        object_get_info::<SocketInfoQuery>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| SocketInfo::from(info))
    }
}

unsafe_handle_properties!(object: Socket,
    props: [
        {query_ty: SOCKET_RX_THRESHOLD, tag: SocketRxThresholdTag, prop_ty: usize, get:get_rx_threshold, set: set_rx_threshold},
        {query_ty: SOCKET_TX_THRESHOLD, tag: SocketTxThresholdTag, prop_ty: usize, get:get_tx_threshold, set: set_tx_threshold},
    ]
);

// TODO(wesleyac): Test peeking

#[cfg(test)]
mod tests {
    use super::*;

    fn socket_basic_helper(opts: SocketOpts) {
        let (s1, s2) = Socket::create(opts).unwrap();

        // Write two packets and read from other end
        assert_eq!(s1.write(b"hello").unwrap(), 5);
        assert_eq!(s1.write(b"world").unwrap(), 5);

        let mut read_vec = vec![0; 11];
        if opts == SocketOpts::DATAGRAM {
            assert_eq!(s2.read(&mut read_vec).unwrap(), 5);
            assert_eq!(&read_vec[0..5], b"hello");

            assert_eq!(s2.read(&mut read_vec).unwrap(), 5);
            assert_eq!(&read_vec[0..5], b"world");
        } else {
            assert_eq!(s2.read(&mut read_vec).unwrap(), 10);
            assert_eq!(&read_vec[0..10], b"helloworld");
        }

        // Try reading when there is nothing to read.
        assert_eq!(s2.read(&mut read_vec), Err(Status::SHOULD_WAIT));

        // Disable writes from the socket peer.
        assert!(s1.half_close().is_ok());
        assert_eq!(s2.write(b"fail"), Err(Status::BAD_STATE));
        assert_eq!(s1.read(&mut read_vec), Err(Status::BAD_STATE));

        // Writing to the peer should still work.
        assert_eq!(s2.read(&mut read_vec), Err(Status::SHOULD_WAIT));
        assert_eq!(s1.write(b"back").unwrap(), 4);
        assert_eq!(s2.read(&mut read_vec).unwrap(), 4);
        assert_eq!(&read_vec[0..4], b"back");
    }

    #[test]
    fn socket_basic() {
        socket_basic_helper(SocketOpts::STREAM);
        socket_basic_helper(SocketOpts::DATAGRAM);
    }

    #[test]
    fn socket_info() {
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();
        let s1info = s1.info().unwrap();
        // Socket should be empty.
        assert_eq!(s1info.rx_buf_available, 0);
        assert_eq!(s1info.rx_buf_size, 0);
        assert_eq!(s1info.tx_buf_size, 0);

        // Put some data in one end.
        assert_eq!(s1.write(b"hello").unwrap(), 5);

        // We should see the info change on each end correspondingly.
        let s1info = s1.info().unwrap();
        let s2info = s2.info().unwrap();
        assert_eq!(s1info.tx_buf_size, 5);
        assert_eq!(s1info.rx_buf_size, 0);
        assert_eq!(s2info.rx_buf_size, 5);
        assert_eq!(s2info.rx_buf_available, 5);
        assert_eq!(s2info.tx_buf_size, 0);
    }

    #[test]
    fn socket_disposition() {
        const PAYLOAD: &'static [u8] = b"Hello";
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();
        // Disable write on s1 but enable on s2.
        assert_eq!(
            s1.set_disposition(
                Some(SocketWriteDisposition::Disabled),
                Some(SocketWriteDisposition::Enabled)
            ),
            Ok(())
        );
        assert_eq!(s2.write(PAYLOAD), Ok(PAYLOAD.len()));
        assert_eq!(s1.write(PAYLOAD), Err(Status::BAD_STATE));
        let mut buf = [0u8; PAYLOAD.len() + 1];
        assert_eq!(s1.read(&mut buf[..]), Ok(PAYLOAD.len()));
        assert_eq!(&buf[..PAYLOAD.len()], PAYLOAD);
        // Setting disposition to None changes nothing.
        assert_eq!(s1.set_disposition(None, None), Ok(()));
        assert_eq!(s2.write(PAYLOAD), Ok(PAYLOAD.len()));
        assert_eq!(s1.write(PAYLOAD), Err(Status::BAD_STATE));
    }
}
