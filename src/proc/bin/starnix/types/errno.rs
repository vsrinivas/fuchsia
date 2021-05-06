// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fuchsia_zircon as zx;
use std::fmt;

use crate::types::uapi;

#[derive(Debug, Eq, PartialEq)]
pub struct Errno {
    value: u32,
    name: &'static str,
}

impl Errno {
    pub fn value(&self) -> i32 {
        self.value as i32
    }
}

impl fmt::Display for Errno {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "error {}: {}", self.value, self.name)
    }
}

pub const EPERM: Errno = Errno { value: uapi::EPERM, name: "EPERM" };
pub const ENOENT: Errno = Errno { value: uapi::ENOENT, name: "ENOENT" };
pub const ESRCH: Errno = Errno { value: uapi::ESRCH, name: "ESRCH" };
pub const EINTR: Errno = Errno { value: uapi::EINTR, name: "EINTR" };
pub const EIO: Errno = Errno { value: uapi::EIO, name: "EIO" };
pub const ENXIO: Errno = Errno { value: uapi::ENXIO, name: "ENXIO" };
pub const E2BIG: Errno = Errno { value: uapi::E2BIG, name: "E2BIG" };
pub const ENOEXEC: Errno = Errno { value: uapi::ENOEXEC, name: "ENOEXEC" };
pub const EBADF: Errno = Errno { value: uapi::EBADF, name: "EBADF" };
pub const ECHILD: Errno = Errno { value: uapi::ECHILD, name: "ECHILD" };
pub const EAGAIN: Errno = Errno { value: uapi::EAGAIN, name: "EAGAIN" };
pub const ENOMEM: Errno = Errno { value: uapi::ENOMEM, name: "ENOMEM" };
pub const EACCES: Errno = Errno { value: uapi::EACCES, name: "EACCES" };
pub const EFAULT: Errno = Errno { value: uapi::EFAULT, name: "EFAULT" };
pub const ENOTBLK: Errno = Errno { value: uapi::ENOTBLK, name: "ENOTBLK" };
pub const EBUSY: Errno = Errno { value: uapi::EBUSY, name: "EBUSY" };
pub const EEXIST: Errno = Errno { value: uapi::EEXIST, name: "EEXIST" };
pub const EXDEV: Errno = Errno { value: uapi::EXDEV, name: "EXDEV" };
pub const ENODEV: Errno = Errno { value: uapi::ENODEV, name: "ENODEV" };
pub const ENOTDIR: Errno = Errno { value: uapi::ENOTDIR, name: "ENOTDIR" };
pub const EISDIR: Errno = Errno { value: uapi::EISDIR, name: "EISDIR" };
pub const EINVAL: Errno = Errno { value: uapi::EINVAL, name: "EINVAL" };
pub const ENFILE: Errno = Errno { value: uapi::ENFILE, name: "ENFILE" };
pub const EMFILE: Errno = Errno { value: uapi::EMFILE, name: "EMFILE" };
pub const ENOTTY: Errno = Errno { value: uapi::ENOTTY, name: "ENOTTY" };
pub const ETXTBSY: Errno = Errno { value: uapi::ETXTBSY, name: "ETXTBSY" };
pub const EFBIG: Errno = Errno { value: uapi::EFBIG, name: "EFBIG" };
pub const ENOSPC: Errno = Errno { value: uapi::ENOSPC, name: "ENOSPC" };
pub const ESPIPE: Errno = Errno { value: uapi::ESPIPE, name: "ESPIPE" };
pub const EROFS: Errno = Errno { value: uapi::EROFS, name: "EROFS" };
pub const EMLINK: Errno = Errno { value: uapi::EMLINK, name: "EMLINK" };
pub const EPIPE: Errno = Errno { value: uapi::EPIPE, name: "EPIPE" };
pub const EDOM: Errno = Errno { value: uapi::EDOM, name: "EDOM" };
pub const ERANGE: Errno = Errno { value: uapi::ERANGE, name: "ERANGE" };
pub const EDEADLK: Errno = Errno { value: uapi::EDEADLK, name: "EDEADLK" };
pub const ENAMETOOLONG: Errno = Errno { value: uapi::ENAMETOOLONG, name: "ENAMETOOLONG" };
pub const ENOLCK: Errno = Errno { value: uapi::ENOLCK, name: "ENOLCK" };
pub const ENOSYS: Errno = Errno { value: uapi::ENOSYS, name: "ENOSYS" };
pub const ENOTEMPTY: Errno = Errno { value: uapi::ENOTEMPTY, name: "ENOTEMPTY" };
pub const ELOOP: Errno = Errno { value: uapi::ELOOP, name: "ELOOP" };
pub const EWOULDBLOCK: Errno = Errno { value: uapi::EWOULDBLOCK, name: "EWOULDBLOCK" };
pub const ENOMSG: Errno = Errno { value: uapi::ENOMSG, name: "ENOMSG" };
pub const EIDRM: Errno = Errno { value: uapi::EIDRM, name: "EIDRM" };
pub const ECHRNG: Errno = Errno { value: uapi::ECHRNG, name: "ECHRNG" };
pub const EL2NSYNC: Errno = Errno { value: uapi::EL2NSYNC, name: "EL2NSYNC" };
pub const EL3HLT: Errno = Errno { value: uapi::EL3HLT, name: "EL3HLT" };
pub const EL3RST: Errno = Errno { value: uapi::EL3RST, name: "EL3RST" };
pub const ELNRNG: Errno = Errno { value: uapi::ELNRNG, name: "ELNRNG" };
pub const EUNATCH: Errno = Errno { value: uapi::EUNATCH, name: "EUNATCH" };
pub const ENOCSI: Errno = Errno { value: uapi::ENOCSI, name: "ENOCSI" };
pub const EL2HLT: Errno = Errno { value: uapi::EL2HLT, name: "EL2HLT" };
pub const EBADE: Errno = Errno { value: uapi::EBADE, name: "EBADE" };
pub const EBADR: Errno = Errno { value: uapi::EBADR, name: "EBADR" };
pub const EXFULL: Errno = Errno { value: uapi::EXFULL, name: "EXFULL" };
pub const ENOANO: Errno = Errno { value: uapi::ENOANO, name: "ENOANO" };
pub const EBADRQC: Errno = Errno { value: uapi::EBADRQC, name: "EBADRQC" };
pub const EBADSLT: Errno = Errno { value: uapi::EBADSLT, name: "EBADSLT" };
pub const EDEADLOCK: Errno = Errno { value: uapi::EDEADLOCK, name: "EDEADLOCK" };
pub const EBFONT: Errno = Errno { value: uapi::EBFONT, name: "EBFONT" };
pub const ENOSTR: Errno = Errno { value: uapi::ENOSTR, name: "ENOSTR" };
pub const ENODATA: Errno = Errno { value: uapi::ENODATA, name: "ENODATA" };
pub const ETIME: Errno = Errno { value: uapi::ETIME, name: "ETIME" };
pub const ENOSR: Errno = Errno { value: uapi::ENOSR, name: "ENOSR" };
pub const ENONET: Errno = Errno { value: uapi::ENONET, name: "ENONET" };
pub const ENOPKG: Errno = Errno { value: uapi::ENOPKG, name: "ENOPKG" };
pub const EREMOTE: Errno = Errno { value: uapi::EREMOTE, name: "EREMOTE" };
pub const ENOLINK: Errno = Errno { value: uapi::ENOLINK, name: "ENOLINK" };
pub const EADV: Errno = Errno { value: uapi::EADV, name: "EADV" };
pub const ESRMNT: Errno = Errno { value: uapi::ESRMNT, name: "ESRMNT" };
pub const ECOMM: Errno = Errno { value: uapi::ECOMM, name: "ECOMM" };
pub const EPROTO: Errno = Errno { value: uapi::EPROTO, name: "EPROTO" };
pub const EMULTIHOP: Errno = Errno { value: uapi::EMULTIHOP, name: "EMULTIHOP" };
pub const EDOTDOT: Errno = Errno { value: uapi::EDOTDOT, name: "EDOTDOT" };
pub const EBADMSG: Errno = Errno { value: uapi::EBADMSG, name: "EBADMSG" };
pub const EOVERFLOW: Errno = Errno { value: uapi::EOVERFLOW, name: "EOVERFLOW" };
pub const ENOTUNIQ: Errno = Errno { value: uapi::ENOTUNIQ, name: "ENOTUNIQ" };
pub const EBADFD: Errno = Errno { value: uapi::EBADFD, name: "EBADFD" };
pub const EREMCHG: Errno = Errno { value: uapi::EREMCHG, name: "EREMCHG" };
pub const ELIBACC: Errno = Errno { value: uapi::ELIBACC, name: "ELIBACC" };
pub const ELIBBAD: Errno = Errno { value: uapi::ELIBBAD, name: "ELIBBAD" };
pub const ELIBSCN: Errno = Errno { value: uapi::ELIBSCN, name: "ELIBSCN" };
pub const ELIBMAX: Errno = Errno { value: uapi::ELIBMAX, name: "ELIBMAX" };
pub const ELIBEXEC: Errno = Errno { value: uapi::ELIBEXEC, name: "ELIBEXEC" };
pub const EILSEQ: Errno = Errno { value: uapi::EILSEQ, name: "EILSEQ" };
pub const ERESTART: Errno = Errno { value: uapi::ERESTART, name: "ERESTART" };
pub const ESTRPIPE: Errno = Errno { value: uapi::ESTRPIPE, name: "ESTRPIPE" };
pub const EUSERS: Errno = Errno { value: uapi::EUSERS, name: "EUSERS" };
pub const ENOTSOCK: Errno = Errno { value: uapi::ENOTSOCK, name: "ENOTSOCK" };
pub const EDESTADDRREQ: Errno = Errno { value: uapi::EDESTADDRREQ, name: "EDESTADDRREQ" };
pub const EMSGSIZE: Errno = Errno { value: uapi::EMSGSIZE, name: "EMSGSIZE" };
pub const EPROTOTYPE: Errno = Errno { value: uapi::EPROTOTYPE, name: "EPROTOTYPE" };
pub const ENOPROTOOPT: Errno = Errno { value: uapi::ENOPROTOOPT, name: "ENOPROTOOPT" };
pub const EPROTONOSUPPORT: Errno = Errno { value: uapi::EPROTONOSUPPORT, name: "EPROTONOSUPPORT" };
pub const ESOCKTNOSUPPORT: Errno = Errno { value: uapi::ESOCKTNOSUPPORT, name: "ESOCKTNOSUPPORT" };
pub const EOPNOTSUPP: Errno = Errno { value: uapi::EOPNOTSUPP, name: "EOPNOTSUPP" };
pub const EPFNOSUPPORT: Errno = Errno { value: uapi::EPFNOSUPPORT, name: "EPFNOSUPPORT" };
pub const EAFNOSUPPORT: Errno = Errno { value: uapi::EAFNOSUPPORT, name: "EAFNOSUPPORT" };
pub const EADDRINUSE: Errno = Errno { value: uapi::EADDRINUSE, name: "EADDRINUSE" };
pub const EADDRNOTAVAIL: Errno = Errno { value: uapi::EADDRNOTAVAIL, name: "EADDRNOTAVAIL" };
pub const ENETDOWN: Errno = Errno { value: uapi::ENETDOWN, name: "ENETDOWN" };
pub const ENETUNREACH: Errno = Errno { value: uapi::ENETUNREACH, name: "ENETUNREACH" };
pub const ENETRESET: Errno = Errno { value: uapi::ENETRESET, name: "ENETRESET" };
pub const ECONNABORTED: Errno = Errno { value: uapi::ECONNABORTED, name: "ECONNABORTED" };
pub const ECONNRESET: Errno = Errno { value: uapi::ECONNRESET, name: "ECONNRESET" };
pub const ENOBUFS: Errno = Errno { value: uapi::ENOBUFS, name: "ENOBUFS" };
pub const EISCONN: Errno = Errno { value: uapi::EISCONN, name: "EISCONN" };
pub const ENOTCONN: Errno = Errno { value: uapi::ENOTCONN, name: "ENOTCONN" };
pub const ESHUTDOWN: Errno = Errno { value: uapi::ESHUTDOWN, name: "ESHUTDOWN" };
pub const ETOOMANYREFS: Errno = Errno { value: uapi::ETOOMANYREFS, name: "ETOOMANYREFS" };
pub const ETIMEDOUT: Errno = Errno { value: uapi::ETIMEDOUT, name: "ETIMEDOUT" };
pub const ECONNREFUSED: Errno = Errno { value: uapi::ECONNREFUSED, name: "ECONNREFUSED" };
pub const EHOSTDOWN: Errno = Errno { value: uapi::EHOSTDOWN, name: "EHOSTDOWN" };
pub const EHOSTUNREACH: Errno = Errno { value: uapi::EHOSTUNREACH, name: "EHOSTUNREACH" };
pub const EALREADY: Errno = Errno { value: uapi::EALREADY, name: "EALREADY" };
pub const EINPROGRESS: Errno = Errno { value: uapi::EINPROGRESS, name: "EINPROGRESS" };
pub const ESTALE: Errno = Errno { value: uapi::ESTALE, name: "ESTALE" };
pub const EUCLEAN: Errno = Errno { value: uapi::EUCLEAN, name: "EUCLEAN" };
pub const ENOTNAM: Errno = Errno { value: uapi::ENOTNAM, name: "ENOTNAM" };
pub const ENAVAIL: Errno = Errno { value: uapi::ENAVAIL, name: "ENAVAIL" };
pub const EISNAM: Errno = Errno { value: uapi::EISNAM, name: "EISNAM" };
pub const EREMOTEIO: Errno = Errno { value: uapi::EREMOTEIO, name: "EREMOTEIO" };
pub const EDQUOT: Errno = Errno { value: uapi::EDQUOT, name: "EDQUOT" };
pub const ENOMEDIUM: Errno = Errno { value: uapi::ENOMEDIUM, name: "ENOMEDIUM" };
pub const EMEDIUMTYPE: Errno = Errno { value: uapi::EMEDIUMTYPE, name: "EMEDIUMTYPE" };
pub const ECANCELED: Errno = Errno { value: uapi::ECANCELED, name: "ECANCELED" };
pub const ENOKEY: Errno = Errno { value: uapi::ENOKEY, name: "ENOKEY" };
pub const EKEYEXPIRED: Errno = Errno { value: uapi::EKEYEXPIRED, name: "EKEYEXPIRED" };
pub const EKEYREVOKED: Errno = Errno { value: uapi::EKEYREVOKED, name: "EKEYREVOKED" };
pub const EKEYREJECTED: Errno = Errno { value: uapi::EKEYREJECTED, name: "EKEYREJECTED" };
pub const EOWNERDEAD: Errno = Errno { value: uapi::EOWNERDEAD, name: "EOWNERDEAD" };
pub const ENOTRECOVERABLE: Errno = Errno { value: uapi::ENOTRECOVERABLE, name: "ENOTRECOVERABLE" };
pub const ERFKILL: Errno = Errno { value: uapi::ERFKILL, name: "ERFKILL" };
pub const EHWPOISON: Errno = Errno { value: uapi::EHWPOISON, name: "EHWPOISON" };

// There isn't really a mapping from zx::Status to Errno. The correct mapping is context-speific
// but this converter is a reasonable first-approximation. The translation matches
// fdio_status_to_errno. See fxbug.dev/30921 for more context.
// TODO: Replace clients with more context-specific mappings.
impl Errno {
    pub fn from_status_like_fdio(status: zx::Status) -> Self {
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
