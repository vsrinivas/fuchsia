// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::types::uapi;

/// Represents the location at which an error was generated.
///
/// This is useful if the anyhow error associated with the `Errno` can't print backtraces.
pub struct ErrnoSource {
    pub file: String,
    pub line: u32,
}

impl std::fmt::Debug for ErrnoSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}:{:?}", self.file, self.line)
    }
}

pub struct Errno {
    value: u32,
    anyhow: Option<anyhow::Error>,
}

impl Errno {
    pub fn new(
        value: u32,
        name: &'static str,
        context: Option<String>,
        source: ErrnoSource,
    ) -> Errno {
        Errno {
            value,
            anyhow: Some(anyhow::format_err!(
                "{:?} {} ({}), context: {}",
                source,
                name,
                value,
                context.as_ref().unwrap_or(&"None".to_string())
            )),
        }
    }

    pub fn value(&self) -> u32 {
        self.value
    }

    pub fn return_value(&self) -> u64 {
        -(self.value as i32) as u64
    }
}

impl PartialEq for Errno {
    fn eq(&self, other: &Self) -> bool {
        self.value == other.value
    }
}

impl Eq for Errno {}

impl From<Errno> for anyhow::Error {
    fn from(e: Errno) -> anyhow::Error {
        let value = e.value;
        e.anyhow.unwrap_or_else(|| anyhow::format_err!("errno {} from unknown location", value))
    }
}

impl std::fmt::Debug for Errno {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        if let Some(err) = self.anyhow.as_ref() {
            std::fmt::Debug::fmt(&err, f)
        } else {
            write!(f, "error {} from unknown location", self.value)
        }
    }
}

pub const EPERM: Errno = Errno { value: uapi::EPERM, anyhow: None };
pub const ENOENT: Errno = Errno { value: uapi::ENOENT, anyhow: None };
pub const ESRCH: Errno = Errno { value: uapi::ESRCH, anyhow: None };
pub const EINTR: Errno = Errno { value: uapi::EINTR, anyhow: None };
pub const EIO: Errno = Errno { value: uapi::EIO, anyhow: None };
pub const ENXIO: Errno = Errno { value: uapi::ENXIO, anyhow: None };
pub const E2BIG: Errno = Errno { value: uapi::E2BIG, anyhow: None };
pub const ENOEXEC: Errno = Errno { value: uapi::ENOEXEC, anyhow: None };
pub const EBADF: Errno = Errno { value: uapi::EBADF, anyhow: None };
pub const ECHILD: Errno = Errno { value: uapi::ECHILD, anyhow: None };
pub const EAGAIN: Errno = Errno { value: uapi::EAGAIN, anyhow: None };
pub const ENOMEM: Errno = Errno { value: uapi::ENOMEM, anyhow: None };
pub const EACCES: Errno = Errno { value: uapi::EACCES, anyhow: None };
pub const EFAULT: Errno = Errno { value: uapi::EFAULT, anyhow: None };
pub const ENOTBLK: Errno = Errno { value: uapi::ENOTBLK, anyhow: None };
pub const EBUSY: Errno = Errno { value: uapi::EBUSY, anyhow: None };
pub const EEXIST: Errno = Errno { value: uapi::EEXIST, anyhow: None };
pub const EXDEV: Errno = Errno { value: uapi::EXDEV, anyhow: None };
pub const ENODEV: Errno = Errno { value: uapi::ENODEV, anyhow: None };
pub const ENOTDIR: Errno = Errno { value: uapi::ENOTDIR, anyhow: None };
pub const EISDIR: Errno = Errno { value: uapi::EISDIR, anyhow: None };
pub const EINVAL: Errno = Errno { value: uapi::EINVAL, anyhow: None };
pub const ENFILE: Errno = Errno { value: uapi::ENFILE, anyhow: None };
pub const EMFILE: Errno = Errno { value: uapi::EMFILE, anyhow: None };
pub const ENOTTY: Errno = Errno { value: uapi::ENOTTY, anyhow: None };
pub const ETXTBSY: Errno = Errno { value: uapi::ETXTBSY, anyhow: None };
pub const EFBIG: Errno = Errno { value: uapi::EFBIG, anyhow: None };
pub const ENOSPC: Errno = Errno { value: uapi::ENOSPC, anyhow: None };
pub const ESPIPE: Errno = Errno { value: uapi::ESPIPE, anyhow: None };
pub const EROFS: Errno = Errno { value: uapi::EROFS, anyhow: None };
pub const EMLINK: Errno = Errno { value: uapi::EMLINK, anyhow: None };
pub const EPIPE: Errno = Errno { value: uapi::EPIPE, anyhow: None };
pub const EDOM: Errno = Errno { value: uapi::EDOM, anyhow: None };
pub const ERANGE: Errno = Errno { value: uapi::ERANGE, anyhow: None };
pub const EDEADLK: Errno = Errno { value: uapi::EDEADLK, anyhow: None };
pub const ENAMETOOLONG: Errno = Errno { value: uapi::ENAMETOOLONG, anyhow: None };
pub const ENOLCK: Errno = Errno { value: uapi::ENOLCK, anyhow: None };
pub const ENOSYS: Errno = Errno { value: uapi::ENOSYS, anyhow: None };
pub const ENOTEMPTY: Errno = Errno { value: uapi::ENOTEMPTY, anyhow: None };
pub const ELOOP: Errno = Errno { value: uapi::ELOOP, anyhow: None };
pub const EWOULDBLOCK: Errno = Errno { value: uapi::EWOULDBLOCK, anyhow: None };
pub const ENOMSG: Errno = Errno { value: uapi::ENOMSG, anyhow: None };
pub const EIDRM: Errno = Errno { value: uapi::EIDRM, anyhow: None };
pub const ECHRNG: Errno = Errno { value: uapi::ECHRNG, anyhow: None };
pub const EL2NSYNC: Errno = Errno { value: uapi::EL2NSYNC, anyhow: None };
pub const EL3HLT: Errno = Errno { value: uapi::EL3HLT, anyhow: None };
pub const EL3RST: Errno = Errno { value: uapi::EL3RST, anyhow: None };
pub const ELNRNG: Errno = Errno { value: uapi::ELNRNG, anyhow: None };
pub const EUNATCH: Errno = Errno { value: uapi::EUNATCH, anyhow: None };
pub const ENOCSI: Errno = Errno { value: uapi::ENOCSI, anyhow: None };
pub const EL2HLT: Errno = Errno { value: uapi::EL2HLT, anyhow: None };
pub const EBADE: Errno = Errno { value: uapi::EBADE, anyhow: None };
pub const EBADR: Errno = Errno { value: uapi::EBADR, anyhow: None };
pub const EXFULL: Errno = Errno { value: uapi::EXFULL, anyhow: None };
pub const ENOANO: Errno = Errno { value: uapi::ENOANO, anyhow: None };
pub const EBADRQC: Errno = Errno { value: uapi::EBADRQC, anyhow: None };
pub const EBADSLT: Errno = Errno { value: uapi::EBADSLT, anyhow: None };
pub const EDEADLOCK: Errno = Errno { value: uapi::EDEADLOCK, anyhow: None };
pub const EBFONT: Errno = Errno { value: uapi::EBFONT, anyhow: None };
pub const ENOSTR: Errno = Errno { value: uapi::ENOSTR, anyhow: None };
pub const ENODATA: Errno = Errno { value: uapi::ENODATA, anyhow: None };
pub const ETIME: Errno = Errno { value: uapi::ETIME, anyhow: None };
pub const ENOSR: Errno = Errno { value: uapi::ENOSR, anyhow: None };
pub const ENONET: Errno = Errno { value: uapi::ENONET, anyhow: None };
pub const ENOPKG: Errno = Errno { value: uapi::ENOPKG, anyhow: None };
pub const EREMOTE: Errno = Errno { value: uapi::EREMOTE, anyhow: None };
pub const ENOLINK: Errno = Errno { value: uapi::ENOLINK, anyhow: None };
pub const EADV: Errno = Errno { value: uapi::EADV, anyhow: None };
pub const ESRMNT: Errno = Errno { value: uapi::ESRMNT, anyhow: None };
pub const ECOMM: Errno = Errno { value: uapi::ECOMM, anyhow: None };
pub const EPROTO: Errno = Errno { value: uapi::EPROTO, anyhow: None };
pub const EMULTIHOP: Errno = Errno { value: uapi::EMULTIHOP, anyhow: None };
pub const EDOTDOT: Errno = Errno { value: uapi::EDOTDOT, anyhow: None };
pub const EBADMSG: Errno = Errno { value: uapi::EBADMSG, anyhow: None };
pub const EOVERFLOW: Errno = Errno { value: uapi::EOVERFLOW, anyhow: None };
pub const ENOTUNIQ: Errno = Errno { value: uapi::ENOTUNIQ, anyhow: None };
pub const EBADFD: Errno = Errno { value: uapi::EBADFD, anyhow: None };
pub const EREMCHG: Errno = Errno { value: uapi::EREMCHG, anyhow: None };
pub const ELIBACC: Errno = Errno { value: uapi::ELIBACC, anyhow: None };
pub const ELIBBAD: Errno = Errno { value: uapi::ELIBBAD, anyhow: None };
pub const ELIBSCN: Errno = Errno { value: uapi::ELIBSCN, anyhow: None };
pub const ELIBMAX: Errno = Errno { value: uapi::ELIBMAX, anyhow: None };
pub const ELIBEXEC: Errno = Errno { value: uapi::ELIBEXEC, anyhow: None };
pub const EILSEQ: Errno = Errno { value: uapi::EILSEQ, anyhow: None };
pub const ERESTART: Errno = Errno { value: uapi::ERESTART, anyhow: None };
pub const ESTRPIPE: Errno = Errno { value: uapi::ESTRPIPE, anyhow: None };
pub const EUSERS: Errno = Errno { value: uapi::EUSERS, anyhow: None };
pub const ENOTSOCK: Errno = Errno { value: uapi::ENOTSOCK, anyhow: None };
pub const EDESTADDRREQ: Errno = Errno { value: uapi::EDESTADDRREQ, anyhow: None };
pub const EMSGSIZE: Errno = Errno { value: uapi::EMSGSIZE, anyhow: None };
pub const EPROTOTYPE: Errno = Errno { value: uapi::EPROTOTYPE, anyhow: None };
pub const ENOPROTOOPT: Errno = Errno { value: uapi::ENOPROTOOPT, anyhow: None };
pub const EPROTONOSUPPORT: Errno = Errno { value: uapi::EPROTONOSUPPORT, anyhow: None };
pub const ESOCKTNOSUPPORT: Errno = Errno { value: uapi::ESOCKTNOSUPPORT, anyhow: None };
pub const EOPNOTSUPP: Errno = Errno { value: uapi::EOPNOTSUPP, anyhow: None };
pub const EPFNOSUPPORT: Errno = Errno { value: uapi::EPFNOSUPPORT, anyhow: None };
pub const EAFNOSUPPORT: Errno = Errno { value: uapi::EAFNOSUPPORT, anyhow: None };
pub const EADDRINUSE: Errno = Errno { value: uapi::EADDRINUSE, anyhow: None };
pub const EADDRNOTAVAIL: Errno = Errno { value: uapi::EADDRNOTAVAIL, anyhow: None };
pub const ENETDOWN: Errno = Errno { value: uapi::ENETDOWN, anyhow: None };
pub const ENETUNREACH: Errno = Errno { value: uapi::ENETUNREACH, anyhow: None };
pub const ENETRESET: Errno = Errno { value: uapi::ENETRESET, anyhow: None };
pub const ECONNABORTED: Errno = Errno { value: uapi::ECONNABORTED, anyhow: None };
pub const ECONNRESET: Errno = Errno { value: uapi::ECONNRESET, anyhow: None };
pub const ENOBUFS: Errno = Errno { value: uapi::ENOBUFS, anyhow: None };
pub const EISCONN: Errno = Errno { value: uapi::EISCONN, anyhow: None };
pub const ENOTCONN: Errno = Errno { value: uapi::ENOTCONN, anyhow: None };
pub const ESHUTDOWN: Errno = Errno { value: uapi::ESHUTDOWN, anyhow: None };
pub const ETOOMANYREFS: Errno = Errno { value: uapi::ETOOMANYREFS, anyhow: None };
pub const ETIMEDOUT: Errno = Errno { value: uapi::ETIMEDOUT, anyhow: None };
pub const ECONNREFUSED: Errno = Errno { value: uapi::ECONNREFUSED, anyhow: None };
pub const EHOSTDOWN: Errno = Errno { value: uapi::EHOSTDOWN, anyhow: None };
pub const EHOSTUNREACH: Errno = Errno { value: uapi::EHOSTUNREACH, anyhow: None };
pub const EALREADY: Errno = Errno { value: uapi::EALREADY, anyhow: None };
pub const EINPROGRESS: Errno = Errno { value: uapi::EINPROGRESS, anyhow: None };
pub const ESTALE: Errno = Errno { value: uapi::ESTALE, anyhow: None };
pub const EUCLEAN: Errno = Errno { value: uapi::EUCLEAN, anyhow: None };
pub const ENOTNAM: Errno = Errno { value: uapi::ENOTNAM, anyhow: None };
pub const ENAVAIL: Errno = Errno { value: uapi::ENAVAIL, anyhow: None };
pub const EISNAM: Errno = Errno { value: uapi::EISNAM, anyhow: None };
pub const EREMOTEIO: Errno = Errno { value: uapi::EREMOTEIO, anyhow: None };
pub const EDQUOT: Errno = Errno { value: uapi::EDQUOT, anyhow: None };
pub const ENOMEDIUM: Errno = Errno { value: uapi::ENOMEDIUM, anyhow: None };
pub const EMEDIUMTYPE: Errno = Errno { value: uapi::EMEDIUMTYPE, anyhow: None };
pub const ECANCELED: Errno = Errno { value: uapi::ECANCELED, anyhow: None };
pub const ENOKEY: Errno = Errno { value: uapi::ENOKEY, anyhow: None };
pub const EKEYEXPIRED: Errno = Errno { value: uapi::EKEYEXPIRED, anyhow: None };
pub const EKEYREVOKED: Errno = Errno { value: uapi::EKEYREVOKED, anyhow: None };
pub const EKEYREJECTED: Errno = Errno { value: uapi::EKEYREJECTED, anyhow: None };
pub const EOWNERDEAD: Errno = Errno { value: uapi::EOWNERDEAD, anyhow: None };
pub const ENOTRECOVERABLE: Errno = Errno { value: uapi::ENOTRECOVERABLE, anyhow: None };
pub const ERFKILL: Errno = Errno { value: uapi::ERFKILL, anyhow: None };
pub const EHWPOISON: Errno = Errno { value: uapi::EHWPOISON, anyhow: None };

// ENOTSUP is a different error in posix, but has the same value as EOPNOTSUPP in linux.
pub const ENOTSUP: Errno = EOPNOTSUPP;

/// A special error that represents an interrupted syscall that should be restarted if possible.
// This is not defined in uapi as it is never exposed to userspace.
pub const ERESTARTSYS: Errno = Errno { value: 512, anyhow: None };

/// `errno` returns an `Errno` struct tagged with the current file name and line number.
///
/// Use `error!` instead if you want the `Errno` to be wrapped in an `Err`.
macro_rules! errno {
    ($err:ident) => {
        Errno::new(
            crate::types::errno::$err.value(),
            stringify!($err),
            None,
            crate::types::errno::ErrnoSource { file: file!().to_string(), line: line!() },
        )
    };
    ($err:ident, $context:expr) => {
        Errno::new(
            crate::types::errno::$err.value(),
            stringify!($err),
            Some($context.to_string()),
            crate::types::errno::ErrnoSource { file: file!().to_string(), line: line!() },
        )
    };
}

/// `error` returns a `Err` containing an `Errno` struct tagged with the current file name and line
/// number.
///
/// Use `errno!` instead if you want an unwrapped, but still tagged, `Errno`.
macro_rules! error {
    ($($args:tt)*) => { Err(errno!($($args)*)) };
}

// There isn't really a mapping from zx::Status to Errno. The correct mapping is context-speific
// but this converter is a reasonable first-approximation. The translation matches
// fdio_status_to_errno. See fxbug.dev/30921 for more context.
// TODO: Replace clients with more context-specific mappings.
macro_rules! from_status_like_fdio {
    ($status:ident) => {{
        from_status_like_fdio!($status, "")
    }};
    ($status:ident, $context:expr) => {{
        match $status {
            zx::Status::NOT_FOUND => errno!(ENOENT, $context),
            zx::Status::NO_MEMORY => errno!(ENOMEM, $context),
            zx::Status::INVALID_ARGS => errno!(EINVAL, $context),
            zx::Status::BUFFER_TOO_SMALL => errno!(EINVAL, $context),
            zx::Status::TIMED_OUT => errno!(ETIMEDOUT, $context),
            zx::Status::UNAVAILABLE => errno!(EBUSY, $context),
            zx::Status::ALREADY_EXISTS => errno!(EEXIST, $context),
            zx::Status::PEER_CLOSED => errno!(EPIPE, $context),
            zx::Status::BAD_STATE => errno!(EPIPE, $context),
            zx::Status::BAD_PATH => errno!(ENAMETOOLONG, $context),
            zx::Status::IO => errno!(EIO, $context),
            zx::Status::NOT_FILE => errno!(EISDIR, $context),
            zx::Status::NOT_DIR => errno!(ENOTDIR, $context),
            zx::Status::NOT_SUPPORTED => errno!(EOPNOTSUPP, $context),
            zx::Status::WRONG_TYPE => errno!(EOPNOTSUPP, $context),
            zx::Status::OUT_OF_RANGE => errno!(EINVAL, $context),
            zx::Status::NO_RESOURCES => errno!(ENOMEM, $context),
            zx::Status::BAD_HANDLE => errno!(EBADF, $context),
            zx::Status::ACCESS_DENIED => errno!(EACCES, $context),
            zx::Status::SHOULD_WAIT => errno!(EAGAIN, $context),
            zx::Status::FILE_BIG => errno!(EFBIG, $context),
            zx::Status::NO_SPACE => errno!(ENOSPC, $context),
            zx::Status::NOT_EMPTY => errno!(ENOTEMPTY, $context),
            zx::Status::IO_REFUSED => errno!(ECONNREFUSED, $context),
            zx::Status::IO_INVALID => errno!(EIO, $context),
            zx::Status::CANCELED => errno!(EBADF, $context),
            zx::Status::PROTOCOL_NOT_SUPPORTED => errno!(EPROTONOSUPPORT, $context),
            zx::Status::ADDRESS_UNREACHABLE => errno!(ENETUNREACH, $context),
            zx::Status::ADDRESS_IN_USE => errno!(EADDRINUSE, $context),
            zx::Status::NOT_CONNECTED => errno!(ENOTCONN, $context),
            zx::Status::CONNECTION_REFUSED => errno!(ECONNREFUSED, $context),
            zx::Status::CONNECTION_RESET => errno!(ECONNRESET, $context),
            zx::Status::CONNECTION_ABORTED => errno!(ECONNABORTED, $context),
            _ => errno!(EIO, $context),
        }
    }};
}

// Public re-export of macros allows them to be used like regular rust items.
pub(crate) use errno;
pub(crate) use error;
pub(crate) use from_status_like_fdio;

/// An extension trait for `Result<T, Errno>`.
pub trait ErrnoResultExt<T> {
    /// Maps `Err(EINTR)` to `Err(ERESTARTSYS)`, which tells the kernel to restart the syscall if
    /// the dispatched signal action that interrupted the syscall has the `SA_RESTART` flag.
    fn restartable(self) -> Result<T, Errno>;
}

impl<T> ErrnoResultExt<T> for Result<T, Errno> {
    fn restartable(self) -> Result<T, Errno> {
        self.map_err(|err| if err == EINTR { errno!(ERESTARTSYS) } else { err })
    }
}
