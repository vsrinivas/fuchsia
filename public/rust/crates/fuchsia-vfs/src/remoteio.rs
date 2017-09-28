// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zircon_sys;

use std::os::raw::c_uint;

const ZXRIO_ONE_HANDLE: c_uint = 0x00000100;

pub(crate) const ZXRIO_STATUS: c_uint = 0x00000000;
pub(crate) const ZXRIO_CLOSE: c_uint = 0x00000001;
pub(crate) const ZXRIO_CLONE: c_uint = (0x00000002 | ZXRIO_ONE_HANDLE);
pub(crate) const ZXRIO_OPEN: c_uint = (0x00000003 | ZXRIO_ONE_HANDLE);
pub(crate) const ZXRIO_MISC: c_uint = 0x00000004;
pub(crate) const ZXRIO_READ: c_uint = 0x00000005;
pub(crate) const ZXRIO_WRITE: c_uint = 0x00000006;
pub(crate) const ZXRIO_SEEK: c_uint = 0x00000007;
pub(crate) const ZXRIO_STAT: c_uint = 0x00000008;
pub(crate) const ZXRIO_READDIR: c_uint = 0x00000009;
pub(crate) const ZXRIO_IOCTL: c_uint = 0x0000000a;
pub(crate) const ZXRIO_IOCTL_1H: c_uint = (0x0000000a | ZXRIO_ONE_HANDLE);
pub(crate) const ZXRIO_UNLINK: c_uint = 0x0000000b;
pub(crate) const ZXRIO_READ_AT: c_uint = 0x0000000c;
pub(crate) const ZXRIO_WRITE_AT: c_uint = 0x0000000d;
pub(crate) const ZXRIO_TRUNCATE: c_uint = 0x0000000e;
pub(crate) const ZXRIO_RENAME: c_uint = (0x0000000f | ZXRIO_ONE_HANDLE);
pub(crate) const ZXRIO_CONNECT: c_uint = 0x00000010;
pub(crate) const ZXRIO_BIND: c_uint = 0x00000011;
pub(crate) const ZXRIO_LISTEN: c_uint = 0x00000012;
pub(crate) const ZXRIO_GETSOCKNAME: c_uint = 0x00000013;
pub(crate) const ZXRIO_GETPEERNAME: c_uint = 0x00000014;
pub(crate) const ZXRIO_GETSOCKOPT: c_uint = 0x00000015;
pub(crate) const ZXRIO_SETSOCKOPT: c_uint = 0x00000016;
pub(crate) const ZXRIO_GETADDRINFO: c_uint = 0x00000017;
pub(crate) const ZXRIO_SETATTR: c_uint = 0x00000018;
pub(crate) const ZXRIO_SYNC: c_uint = 0x00000019;
pub(crate) const ZXRIO_LINK: c_uint = (0x0000001a | ZXRIO_ONE_HANDLE);
pub(crate) const ZXRIO_MMAP: c_uint = 0x0000001b;
pub(crate) const ZXRIO_FCNTL: c_uint = 0x0000001c;
pub(crate) const ZXRIO_NUM_OPS: c_uint = 29;

pub(crate) const ZXRIO_OBJECT_EXTRA: usize = 32;
pub(crate) const ZXRIO_OBJECT_MINSIZE: usize = 8;

pub(crate) const FDIO_MAX_HANDLES: usize = 3;
pub(crate) const FDIO_CHUNK_SIZE: usize = 8192;

pub(crate) const ZXRIO_HDR_SZ: usize = 48;
pub(crate) const ZXRIO_MSG_SZ: usize = ZXRIO_HDR_SZ + FDIO_CHUNK_SIZE as usize;

macro_rules! ZXRIO_HC {
    ($x:expr) => (($x >> 8) & 3);
}
macro_rules! ZXRIO_OP {
    ($x:expr) => ($x & 0x3FF);
}

#[allow(non_camel_case_types)]
pub(crate) type zx_txid_t = u32;
#[repr(C)]
#[allow(non_camel_case_types)]
pub(crate) struct zxrio_msg_t {
    pub txid: zx_txid_t,
    pub op: u32,
    pub datalen: u32,
    pub arg: i32,
    pub arg2: zxrio_msg_arg2,
    pub reserved: i32,
    pub hcount: u32,
    pub handle: [zircon_sys::zx_handle_t; 4],
    pub data: [u8; FDIO_CHUNK_SIZE as usize],
}

#[allow(unused_qualifications)]
impl ::std::fmt::Debug for zxrio_msg_t {
    fn fmt(&self, fmt: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        let op = unsafe {
            ::std::ffi::CStr::from_ptr(fdio_opname(self.op))
                .to_str()
                .unwrap()
        };

        fmt.debug_struct("zxrio_msg")
            .field("txid", &self.txid)
            .field("op", &op)
            .field("datalen", &self.datalen)
            .field("arg", &self.arg)
            // XXX(raggi): this could read uninitialized memory. As this struct
            // is to be written to and read from the wire, doing so is an
            // information leak that must be fixed. The struct should always be
            // initialized or read from the wire.
            .field("arg2", unsafe { &self.arg2.off })
            .field("reserved", &self.reserved)
            .field("hcount", &self.hcount)
            .field("handle", &self.handle)
            .field("data", &(&self.data[0..self.datalen as usize] as &[_]))
            .finish()
    }
}

#[repr(C)]
#[allow(non_camel_case_types)]
pub(crate) union zxrio_msg_arg2 {
    pub off: i64,
    pub mode: u32,
    pub protocol: u32,
    pub op: u32,
}

extern "C" {
    pub(crate) fn fdio_opname(op: u32) -> *const ::std::os::raw::c_char;
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn assert_sizes() {
        assert_eq!(ZXRIO_MSG_SZ, ::std::mem::size_of::<zxrio_msg_t>());
        assert_eq!(
            ZXRIO_HDR_SZ,
            ::std::mem::size_of::<zxrio_msg_t>() - FDIO_CHUNK_SIZE as usize
        );
    }

}
