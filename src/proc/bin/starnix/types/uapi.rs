// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]

use zerocopy::{AsBytes, FromBytes};

use crate::types::UserAddress;

#[cfg(target_arch = "x86_64")]
use linux_uapi::x86_64 as uapi;
pub use uapi::*;

pub type dev_t = uapi::__kernel_old_dev_t;
pub type gid_t = uapi::__kernel_gid_t;
pub type ino_t = uapi::__kernel_ino_t;
pub type mode_t = uapi::__kernel_mode_t;
pub type off_t = uapi::__kernel_off_t;
pub type pid_t = uapi::__kernel_pid_t;
pub type uid_t = uapi::__kernel_uid_t;

#[derive(Debug, Eq, PartialEq, Hash, Copy, Clone, AsBytes, FromBytes)]
#[repr(C)]
pub struct utsname_t {
    pub sysname: [u8; 65],
    pub nodename: [u8; 65],
    pub release: [u8; 65],
    pub version: [u8; 65],
    pub machine: [u8; 65],
}

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
#[repr(C)]
pub struct stat_t {
    pub st_dev: dev_t,
    pub st_ino: ino_t,
    pub st_nlink: u64,
    pub st_mode: mode_t,
    pub st_uid: uid_t,
    pub st_gid: gid_t,
    pub _pad0: u32,
    pub st_rdev: dev_t,
    pub st_size: off_t,
    pub st_blksize: i64,
    pub st_blocks: i64,
    pub st_atim: timespec,
    pub st_mtim: timespec,
    pub st_ctim: timespec,
    pub _pad3: [i64; 3],
}

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes, PartialEq)]
#[repr(C)]
pub struct statfs {
    f_type: i64,
    f_bsize: i64,
    f_blocks: i64,
    f_bfree: i64,
    f_bavail: i64,
    f_files: i64,
    f_ffree: i64,
    f_fsid: i64,
    f_namelen: i64,
    f_frsize: i64,
    f_flags: i64,
    f_spare: [i64; 4],
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, AsBytes, FromBytes)]
#[repr(C)]
pub struct sigaltstack_t {
    pub ss_sp: UserAddress,
    pub ss_flags: u32,
    pub _pad0: u32,
    pub ss_size: usize,
}

impl sigaltstack_t {
    /// Returns true if the passed in `ptr` is considered to be on this stack.
    pub fn contains_pointer(&self, ptr: u64) -> bool {
        let min = self.ss_sp.ptr() as u64;
        let max = (self.ss_sp + self.ss_size).ptr() as u64;
        ptr >= min && ptr <= max
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, AsBytes, FromBytes)]
#[repr(C)]
pub struct sigaction_t {
    pub sa_handler: UserAddress,
    pub sa_flags: uapi::c_ulong,
    pub sa_restorer: UserAddress,
    pub sa_mask: sigset_t,
}
