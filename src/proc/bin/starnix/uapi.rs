// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]
#![allow(dead_code)]

use {
    fuchsia_zircon::{self as zx, sys::zx_vaddr_t},
    std::fmt,
    std::ops,
    zerocopy::{AsBytes, FromBytes},
};

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

pub struct Errno(u32);

impl Errno {
    pub fn value(&self) -> i32 {
        self.0 as i32
    }
}
impl fmt::Display for Errno {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // TODO(tbodt): Return the string name of the error (would be nice to be able to do this
        // without typing them all twice.)
        write!(f, "error {}", self.0)
    }
}

pub const EPERM: Errno = Errno(uapi::EPERM);
pub const ENOENT: Errno = Errno(uapi::ENOENT);
pub const ESRCH: Errno = Errno(uapi::ESRCH);
pub const EINTR: Errno = Errno(uapi::EINTR);
pub const EIO: Errno = Errno(uapi::EIO);
pub const ENXIO: Errno = Errno(uapi::ENXIO);
pub const E2BIG: Errno = Errno(uapi::E2BIG);
pub const ENOEXEC: Errno = Errno(uapi::ENOEXEC);
pub const EBADF: Errno = Errno(uapi::EBADF);
pub const ECHILD: Errno = Errno(uapi::ECHILD);
pub const EAGAIN: Errno = Errno(uapi::EAGAIN);
pub const ENOMEM: Errno = Errno(uapi::ENOMEM);
pub const EACCES: Errno = Errno(uapi::EACCES);
pub const EFAULT: Errno = Errno(uapi::EFAULT);
pub const ENOTBLK: Errno = Errno(uapi::ENOTBLK);
pub const EBUSY: Errno = Errno(uapi::EBUSY);
pub const EEXIST: Errno = Errno(uapi::EEXIST);
pub const EXDEV: Errno = Errno(uapi::EXDEV);
pub const ENODEV: Errno = Errno(uapi::ENODEV);
pub const ENOTDIR: Errno = Errno(uapi::ENOTDIR);
pub const EISDIR: Errno = Errno(uapi::EISDIR);
pub const EINVAL: Errno = Errno(uapi::EINVAL);
pub const ENFILE: Errno = Errno(uapi::ENFILE);
pub const EMFILE: Errno = Errno(uapi::EMFILE);
pub const ENOTTY: Errno = Errno(uapi::ENOTTY);
pub const ETXTBSY: Errno = Errno(uapi::ETXTBSY);
pub const EFBIG: Errno = Errno(uapi::EFBIG);
pub const ENOSPC: Errno = Errno(uapi::ENOSPC);
pub const ESPIPE: Errno = Errno(uapi::ESPIPE);
pub const EROFS: Errno = Errno(uapi::EROFS);
pub const EMLINK: Errno = Errno(uapi::EMLINK);
pub const EPIPE: Errno = Errno(uapi::EPIPE);
pub const EDOM: Errno = Errno(uapi::EDOM);
pub const ERANGE: Errno = Errno(uapi::ERANGE);
pub const EDEADLK: Errno = Errno(uapi::EDEADLK);
pub const ENAMETOOLONG: Errno = Errno(uapi::ENAMETOOLONG);
pub const ENOLCK: Errno = Errno(uapi::ENOLCK);
pub const ENOSYS: Errno = Errno(uapi::ENOSYS);
pub const ENOTEMPTY: Errno = Errno(uapi::ENOTEMPTY);
pub const ELOOP: Errno = Errno(uapi::ELOOP);
pub const EWOULDBLOCK: Errno = Errno(uapi::EWOULDBLOCK);
pub const ENOMSG: Errno = Errno(uapi::ENOMSG);
pub const EIDRM: Errno = Errno(uapi::EIDRM);
pub const ECHRNG: Errno = Errno(uapi::ECHRNG);
pub const EL2NSYNC: Errno = Errno(uapi::EL2NSYNC);
pub const EL3HLT: Errno = Errno(uapi::EL3HLT);
pub const EL3RST: Errno = Errno(uapi::EL3RST);
pub const ELNRNG: Errno = Errno(uapi::ELNRNG);
pub const EUNATCH: Errno = Errno(uapi::EUNATCH);
pub const ENOCSI: Errno = Errno(uapi::ENOCSI);
pub const EL2HLT: Errno = Errno(uapi::EL2HLT);
pub const EBADE: Errno = Errno(uapi::EBADE);
pub const EBADR: Errno = Errno(uapi::EBADR);
pub const EXFULL: Errno = Errno(uapi::EXFULL);
pub const ENOANO: Errno = Errno(uapi::ENOANO);
pub const EBADRQC: Errno = Errno(uapi::EBADRQC);
pub const EBADSLT: Errno = Errno(uapi::EBADSLT);
pub const EDEADLOCK: Errno = Errno(uapi::EDEADLOCK);
pub const EBFONT: Errno = Errno(uapi::EBFONT);
pub const ENOSTR: Errno = Errno(uapi::ENOSTR);
pub const ENODATA: Errno = Errno(uapi::ENODATA);
pub const ETIME: Errno = Errno(uapi::ETIME);
pub const ENOSR: Errno = Errno(uapi::ENOSR);
pub const ENONET: Errno = Errno(uapi::ENONET);
pub const ENOPKG: Errno = Errno(uapi::ENOPKG);
pub const EREMOTE: Errno = Errno(uapi::EREMOTE);
pub const ENOLINK: Errno = Errno(uapi::ENOLINK);
pub const EADV: Errno = Errno(uapi::EADV);
pub const ESRMNT: Errno = Errno(uapi::ESRMNT);
pub const ECOMM: Errno = Errno(uapi::ECOMM);
pub const EPROTO: Errno = Errno(uapi::EPROTO);
pub const EMULTIHOP: Errno = Errno(uapi::EMULTIHOP);
pub const EDOTDOT: Errno = Errno(uapi::EDOTDOT);
pub const EBADMSG: Errno = Errno(uapi::EBADMSG);
pub const EOVERFLOW: Errno = Errno(uapi::EOVERFLOW);
pub const ENOTUNIQ: Errno = Errno(uapi::ENOTUNIQ);
pub const EBADFD: Errno = Errno(uapi::EBADFD);
pub const EREMCHG: Errno = Errno(uapi::EREMCHG);
pub const ELIBACC: Errno = Errno(uapi::ELIBACC);
pub const ELIBBAD: Errno = Errno(uapi::ELIBBAD);
pub const ELIBSCN: Errno = Errno(uapi::ELIBSCN);
pub const ELIBMAX: Errno = Errno(uapi::ELIBMAX);
pub const ELIBEXEC: Errno = Errno(uapi::ELIBEXEC);
pub const EILSEQ: Errno = Errno(uapi::EILSEQ);
pub const ERESTART: Errno = Errno(uapi::ERESTART);
pub const ESTRPIPE: Errno = Errno(uapi::ESTRPIPE);
pub const EUSERS: Errno = Errno(uapi::EUSERS);
pub const ENOTSOCK: Errno = Errno(uapi::ENOTSOCK);
pub const EDESTADDRREQ: Errno = Errno(uapi::EDESTADDRREQ);
pub const EMSGSIZE: Errno = Errno(uapi::EMSGSIZE);
pub const EPROTOTYPE: Errno = Errno(uapi::EPROTOTYPE);
pub const ENOPROTOOPT: Errno = Errno(uapi::ENOPROTOOPT);
pub const EPROTONOSUPPORT: Errno = Errno(uapi::EPROTONOSUPPORT);
pub const ESOCKTNOSUPPORT: Errno = Errno(uapi::ESOCKTNOSUPPORT);
pub const EOPNOTSUPP: Errno = Errno(uapi::EOPNOTSUPP);
pub const EPFNOSUPPORT: Errno = Errno(uapi::EPFNOSUPPORT);
pub const EAFNOSUPPORT: Errno = Errno(uapi::EAFNOSUPPORT);
pub const EADDRINUSE: Errno = Errno(uapi::EADDRINUSE);
pub const EADDRNOTAVAIL: Errno = Errno(uapi::EADDRNOTAVAIL);
pub const ENETDOWN: Errno = Errno(uapi::ENETDOWN);
pub const ENETUNREACH: Errno = Errno(uapi::ENETUNREACH);
pub const ENETRESET: Errno = Errno(uapi::ENETRESET);
pub const ECONNABORTED: Errno = Errno(uapi::ECONNABORTED);
pub const ECONNRESET: Errno = Errno(uapi::ECONNRESET);
pub const ENOBUFS: Errno = Errno(uapi::ENOBUFS);
pub const EISCONN: Errno = Errno(uapi::EISCONN);
pub const ENOTCONN: Errno = Errno(uapi::ENOTCONN);
pub const ESHUTDOWN: Errno = Errno(uapi::ESHUTDOWN);
pub const ETOOMANYREFS: Errno = Errno(uapi::ETOOMANYREFS);
pub const ETIMEDOUT: Errno = Errno(uapi::ETIMEDOUT);
pub const ECONNREFUSED: Errno = Errno(uapi::ECONNREFUSED);
pub const EHOSTDOWN: Errno = Errno(uapi::EHOSTDOWN);
pub const EHOSTUNREACH: Errno = Errno(uapi::EHOSTUNREACH);
pub const EALREADY: Errno = Errno(uapi::EALREADY);
pub const EINPROGRESS: Errno = Errno(uapi::EINPROGRESS);
pub const ESTALE: Errno = Errno(uapi::ESTALE);
pub const EUCLEAN: Errno = Errno(uapi::EUCLEAN);
pub const ENOTNAM: Errno = Errno(uapi::ENOTNAM);
pub const ENAVAIL: Errno = Errno(uapi::ENAVAIL);
pub const EISNAM: Errno = Errno(uapi::EISNAM);
pub const EREMOTEIO: Errno = Errno(uapi::EREMOTEIO);
pub const EDQUOT: Errno = Errno(uapi::EDQUOT);
pub const ENOMEDIUM: Errno = Errno(uapi::ENOMEDIUM);
pub const EMEDIUMTYPE: Errno = Errno(uapi::EMEDIUMTYPE);
pub const ECANCELED: Errno = Errno(uapi::ECANCELED);
pub const ENOKEY: Errno = Errno(uapi::ENOKEY);
pub const EKEYEXPIRED: Errno = Errno(uapi::EKEYEXPIRED);
pub const EKEYREVOKED: Errno = Errno(uapi::EKEYREVOKED);
pub const EKEYREJECTED: Errno = Errno(uapi::EKEYREJECTED);
pub const EOWNERDEAD: Errno = Errno(uapi::EOWNERDEAD);
pub const ENOTRECOVERABLE: Errno = Errno(uapi::ENOTRECOVERABLE);
pub const ERFKILL: Errno = Errno(uapi::ERFKILL);
pub const EHWPOISON: Errno = Errno(uapi::EHWPOISON);

// There isn't really a mapping from zx::Status to Errno. The correct mapping is context-speific
// but this converter is a reasonable first-approximation. The translation matches
// fdio_status_to_errno. See fxbug.dev/30921 for more context.
// TODO: Replace clients with more context-specific mappings.
impl Errno {
    pub fn from_status(status: zx::Status) -> Self {
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
            zx::Status::NOT_SUPPORTED => EOPNOTSUPP,
            zx::Status::WRONG_TYPE => EOPNOTSUPP,
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
    const NULL_PTR: u64 = 0;

    pub fn ptr(&self) -> zx_vaddr_t {
        self.0 as zx_vaddr_t
    }

    pub fn round_up(&self, increment: u64) -> UserAddress {
        UserAddress((self.0 + (increment - 1)) & !(increment - 1))
    }

    pub fn is_null(&self) -> bool {
        self.0 == UserAddress::NULL_PTR
    }
}

impl Default for UserAddress {
    fn default() -> UserAddress {
        UserAddress(UserAddress::NULL_PTR)
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

/// The result of executing a syscall.
///
/// It would be nice to have this also cover errors, but currently there is no stable way
/// to implement `std::ops::Try` (for the `?` operator) for custom enums, making it difficult
/// to work with.
pub enum SyscallResult {
    /// The process exited as a result of the syscall. The associated `u64` represents the process'
    /// exit code.
    Exit(i32),

    /// The syscall completed successfully. The associated `u64` is the return value from the
    /// syscall.
    Success(u64),
}

pub const SUCCESS: SyscallResult = SyscallResult::Success(0);

impl From<UserAddress> for SyscallResult {
    fn from(value: UserAddress) -> Self {
        SyscallResult::Success(value.ptr() as u64)
    }
}

impl From<i32> for SyscallResult {
    fn from(value: i32) -> Self {
        SyscallResult::Success(value as u64)
    }
}

impl From<u32> for SyscallResult {
    fn from(value: u32) -> Self {
        SyscallResult::Success(value as u64)
    }
}

impl From<u64> for SyscallResult {
    fn from(value: u64) -> Self {
        SyscallResult::Success(value)
    }
}

impl From<usize> for SyscallResult {
    fn from(value: usize) -> Self {
        SyscallResult::Success(value as u64)
    }
}

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
pub struct timeval_t {
    pub tv_sec: i64,
    pub tv_usec: i64,
}

#[derive(Debug, Default, Eq, PartialEq, Hash, Copy, Clone, AsBytes)]
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
    pub st_atim: timespec_t,
    pub st_mtim: timespec_t,
    pub st_ctim: timespec_t,
    pub _pad3: [i64; 3],
}

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
#[repr(C)]
pub struct iovec_t {
    pub iov_base: UserAddress,
    pub iov_len: usize,
}
