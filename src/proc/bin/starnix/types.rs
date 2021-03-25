// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: Generate from Linux uapi using bindgen.

#![allow(non_camel_case_types)]
#![allow(dead_code)]

use {
    fuchsia_zircon::{self as zx, sys::zx_vaddr_t},
    std::fmt,
    std::ops,
    zerocopy::{AsBytes, FromBytes},
};

pub type uid_t = u32;
pub type gid_t = u32;
pub type dev_t = u64;
pub type ino_t = u64;
pub type mode_t = u16;
pub type off_t = i64;

pub struct Errno(i32);

impl Errno {
    pub fn value(&self) -> i32 {
        self.0
    }
}
impl fmt::Display for Errno {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // TODO(tbodt): Return the string name of the error (would be nice to be able to do this
        // without typing them all twice.)
        write!(f, "error {}", self.0)
    }
}

pub const EPERM: Errno = Errno(1);
pub const ENOENT: Errno = Errno(2);
pub const ESRCH: Errno = Errno(3);
pub const EINTR: Errno = Errno(4);
pub const EIO: Errno = Errno(5);
pub const ENXIO: Errno = Errno(6);
pub const E2BIG: Errno = Errno(7);
pub const ENOEXEC: Errno = Errno(8);
pub const EBADF: Errno = Errno(9);
pub const ECHILD: Errno = Errno(10);
pub const EAGAIN: Errno = Errno(11);
pub const ENOMEM: Errno = Errno(12);
pub const EACCES: Errno = Errno(13);
pub const EFAULT: Errno = Errno(14);
pub const ENOTBLK: Errno = Errno(15);
pub const EBUSY: Errno = Errno(16);
pub const EEXIST: Errno = Errno(17);
pub const EXDEV: Errno = Errno(18);
pub const ENODEV: Errno = Errno(19);
pub const ENOTDIR: Errno = Errno(20);
pub const EISDIR: Errno = Errno(21);
pub const EINVAL: Errno = Errno(22);
pub const ENFILE: Errno = Errno(23);
pub const EMFILE: Errno = Errno(24);
pub const ENOTTY: Errno = Errno(25);
pub const ETXTBSY: Errno = Errno(26);
pub const EFBIG: Errno = Errno(27);
pub const ENOSPC: Errno = Errno(28);
pub const ESPIPE: Errno = Errno(29);
pub const EROFS: Errno = Errno(30);
pub const EMLINK: Errno = Errno(31);
pub const EPIPE: Errno = Errno(32);
pub const EDOM: Errno = Errno(33);
pub const ERANGE: Errno = Errno(34);
pub const EDEADLK: Errno = Errno(35);
pub const ENAMETOOLONG: Errno = Errno(36);
pub const ENOLCK: Errno = Errno(37);
pub const ENOSYS: Errno = Errno(38);
pub const ENOTEMPTY: Errno = Errno(39);
pub const EPROTONOSUPPORT: Errno = Errno(93);
pub const ENOTSUP: Errno = Errno(95);
pub const EADDRINUSE: Errno = Errno(98);
pub const ENETUNREACH: Errno = Errno(101);
pub const ECONNABORTED: Errno = Errno(103);
pub const ECONNRESET: Errno = Errno(104);
pub const ENOTCONN: Errno = Errno(107);
pub const ETIMEDOUT: Errno = Errno(110);
pub const ECONNREFUSED: Errno = Errno(111);

// There isn't really a mapping from zx::Status to Errno. The correct mapping is context-speific
// but this converter is a reasonable first-approximation. The translation matches
// fdio_status_to_errno. See fxbug.dev/30921 for more context.
// TODO: Replace clients with more context-specific mappings.
impl From<zx::Status> for Errno {
    fn from(status: zx::Status) -> Self {
        match status {
            zx::Status::NOT_FOUND => ENOENT,
            zx::Status::NO_MEMORY => ENOMEM,
            zx::Status::INVALID_ARGS => EINVAL,
            zx::Status::BUFFER_TOO_SMALL => EINVAL,
            zx::Status::TIMED_OUT => ETIMEDOUT,
            zx::Status::UNAVAILABLE => EBUSY,
            zx::Status::ALREADY_EXISTS => EEXIST,
            zx::Status::PEER_CLOSED => EPIPE,
            zx::Status::BAD_STATE => EPIPE,
            zx::Status::BAD_PATH => ENAMETOOLONG,
            zx::Status::IO => EIO,
            zx::Status::NOT_FILE => EISDIR,
            zx::Status::NOT_DIR => ENOTDIR,
            zx::Status::NOT_SUPPORTED => ENOTSUP,
            zx::Status::WRONG_TYPE => ENOTSUP,
            zx::Status::OUT_OF_RANGE => EINVAL,
            zx::Status::NO_RESOURCES => ENOMEM,
            zx::Status::BAD_HANDLE => EBADF,
            zx::Status::ACCESS_DENIED => EACCES,
            zx::Status::SHOULD_WAIT => EAGAIN,
            zx::Status::FILE_BIG => EFBIG,
            zx::Status::NO_SPACE => ENOSPC,
            zx::Status::NOT_EMPTY => ENOTEMPTY,
            zx::Status::IO_REFUSED => ECONNREFUSED,
            zx::Status::IO_INVALID => EIO,
            zx::Status::CANCELED => EBADF,
            zx::Status::PROTOCOL_NOT_SUPPORTED => EPROTONOSUPPORT,
            zx::Status::ADDRESS_UNREACHABLE => ENETUNREACH,
            zx::Status::ADDRESS_IN_USE => EADDRINUSE,
            zx::Status::NOT_CONNECTED => ENOTCONN,
            zx::Status::CONNECTION_REFUSED => ECONNREFUSED,
            zx::Status::CONNECTION_RESET => ECONNRESET,
            zx::Status::CONNECTION_ABORTED => ECONNABORTED,
            _ => EIO,
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd, AsBytes, FromBytes)]
#[repr(transparent)]
pub struct UserAddress(u64);

impl UserAddress {
    pub fn ptr(&self) -> zx_vaddr_t {
        self.0 as zx_vaddr_t
    }

    pub fn round_up(&self, increment: u64) -> UserAddress {
        UserAddress((self.0 + (increment - 1)) & !(increment - 1))
    }
}

impl Default for UserAddress {
    fn default() -> UserAddress {
        UserAddress(0)
    }
}

impl From<u64> for UserAddress {
    fn from(value: u64) -> Self {
        UserAddress(value)
    }
}

impl From<usize> for UserAddress {
    fn from(value: usize) -> Self {
        UserAddress(value as u64)
    }
}

impl ops::Add<u64> for UserAddress {
    type Output = UserAddress;

    fn add(self, rhs: u64) -> UserAddress {
        UserAddress(self.0 + rhs)
    }
}

impl ops::Sub<UserAddress> for UserAddress {
    type Output = usize;

    fn sub(self, rhs: UserAddress) -> usize {
        self.ptr() - rhs.ptr()
    }
}

impl fmt::Display for UserAddress {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "0x{:x}", self.0)
    }
}

pub struct SyscallResult(u64);

pub const SUCCESS: SyscallResult = SyscallResult(0);

impl SyscallResult {
    pub fn value(&self) -> u64 {
        self.0
    }
}

impl From<UserAddress> for SyscallResult {
    fn from(value: UserAddress) -> Self {
        SyscallResult(value.ptr() as u64)
    }
}

impl From<u32> for SyscallResult {
    fn from(value: u32) -> Self {
        SyscallResult(value as u64)
    }
}

impl From<u64> for SyscallResult {
    fn from(value: u64) -> Self {
        SyscallResult(value)
    }
}

impl From<usize> for SyscallResult {
    fn from(value: usize) -> Self {
        SyscallResult(value as u64)
    }
}

pub type syscall_number_t = u64;

pub const SYS_WRITE: syscall_number_t = 1;
pub const SYS_FSTAT: syscall_number_t = 5;
pub const SYS_MMAP: syscall_number_t = 9;
pub const SYS_MPROTECT: syscall_number_t = 10;
pub const SYS_BRK: syscall_number_t = 12;
pub const SYS_WRITEV: syscall_number_t = 20;
pub const SYS_ACCESS: syscall_number_t = 21;
pub const SYS_EXIT: syscall_number_t = 60;
pub const SYS_UNAME: syscall_number_t = 63;
pub const SYS_READLINK: syscall_number_t = 89;
pub const SYS_GETUID: syscall_number_t = 102;
pub const SYS_GETGID: syscall_number_t = 104;
pub const SYS_GETEUID: syscall_number_t = 107;
pub const SYS_GETEGID: syscall_number_t = 108;
pub const SYS_ARCH_PRCTL: syscall_number_t = 158;
pub const SYS_EXIT_GROUP: syscall_number_t = 231;

pub const ARCH_SET_GS: i32 = 0x1001;
pub const ARCH_SET_FS: i32 = 0x1002;

pub const AT_NULL: u64 = 0;
pub const AT_PHDR: u64 = 3;
pub const AT_PHNUM: u64 = 5;
pub const AT_PAGESZ: u64 = 6;
pub const AT_UID: u64 = 11;
pub const AT_EUID: u64 = 12;
pub const AT_GID: u64 = 13;
pub const AT_EGID: u64 = 14;
pub const AT_SECURE: u64 = 23;
pub const AT_RANDOM: u64 = 25;

#[derive(Debug, Eq, PartialEq, Hash, Copy, Clone, AsBytes)]
#[repr(C)]
pub struct utsname_t {
    pub sysname: [u8; 65],
    pub nodename: [u8; 65],
    pub release: [u8; 65],
    pub version: [u8; 65],
    pub machine: [u8; 65],
}

#[derive(Debug, Default, Eq, PartialEq, Hash, Copy, Clone, AsBytes)]
#[repr(C)]
pub struct timespec_t {
    pub tv_sec: i64,
    pub tv_nsec: i64,
}

#[derive(Debug, Default, Eq, PartialEq, Hash, Copy, Clone, AsBytes)]
#[repr(C)]
pub struct stat_t {
    pub st_dev: dev_t,
    pub st_ino: ino_t,
    pub st_nlink: u64,
    pub st_mode: u32, // should be mod_t
    pub st_uid: uid_t,
    pub st_gid: gid_t,
    pub _pad0: u32,
    pub st_rdev: dev_t,
    pub st_size: off_t,
    pub st_blksize: i64,
    pub st_blocks: i64,
    pub st_atim: timespec_t,
    pub st_mtim: timespec_t,
    pub st_ctim: timespec_t,
    pub _pad3: [i64; 3],
}

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
#[repr(C)]
pub struct iovec {
    pub iov_base: UserAddress,
    pub iov_len: usize,
}

pub const UIO_MAXIOV: u32 = 1024;

pub const PROT_READ: i32 = 0x1;
pub const PROT_WRITE: i32 = 0x2;
pub const PROT_EXEC: i32 = 0x4;
pub const PROT_NONE: i32 = 0;

pub const MAP_SHARED: i32 = 0x1;
pub const MAP_PRIVATE: i32 = 0x2;
pub const MAP_FIXED: i32 = 0x10;
pub const MAP_ANONYMOUS: i32 = 0x20;
