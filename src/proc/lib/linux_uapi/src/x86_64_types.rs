// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::x86_64::__kernel_sa_family_t;
use zerocopy::{AsBytes, FromBytes};

pub type c_void = ::std::ffi::c_void;
pub type c_char = i8;
pub type c_schar = i8;
pub type c_uchar = u8;
pub type c_short = i16;
pub type c_ushort = u16;
pub type c_int = i32;
pub type c_uint = u32;
pub type c_long = i64;
pub type c_ulong = u64;
pub type c_longlong = i64;
pub type c_ulonglong = u64;

/// Unix domain sockets.
pub const AF_UNIX: u32 = 1;

pub const SOCK_CLOEXEC: u32 = 2000000;
pub const SOCK_NONBLOCK: u32 = 4000;

pub const SOCK_STREAM: u32 = 1;
pub const SOCK_DGRAM: u32 = 2;
pub const SOCK_SEQPACKET: u32 = 5;

#[repr(C)]
#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
pub struct sockaddr_un {
    pub sun_family: __kernel_sa_family_t,
    pub sun_path: [u8; 108],
}

impl Default for sockaddr_un {
    fn default() -> Self {
        sockaddr_un { sun_family: 0, sun_path: [0; 108] }
    }
}
